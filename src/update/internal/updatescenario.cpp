/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "updatescenario.h"

#include <QTimer>

#include "global/concurrency/concurrent.h"

#include "updateerrors.h"

#include "translation.h"
#include "defer.h"
#include "log.h"

static constexpr int AUTO_CHECK_UPDATE_INTERVAL = 1000;

using namespace mu;
using namespace mu::update;
using namespace muse::actions;

static ValList releasesNotesToValList(const PrevReleasesNotesList& list)
{
    ValList valList;
    for (const PrevReleaseNotes& release : list) {
        valList.emplace_back(Val(ValMap {
            { "version", Val(release.version) },
            { "notes", Val(release.notes) }
        }));
    }

    return valList;
}

static PrevReleasesNotesList releasesNotesFromValList(const ValList& list)
{
    PrevReleasesNotesList notes;
    for (const Val& val : list) {
        ValMap releaseMap = val.toMap();
        notes.emplace_back(releaseMap.at("version").toString(), releaseMap.at("notes").toString());
    }

    return notes;
}

static ValMap releaseInfoToValMap(const ReleaseInfo& info)
{
    return {
        { "version", Val(info.version) },
        { "fileName", Val(info.fileName) },
        { "fileUrl", Val(info.fileUrl) },
        { "notes", Val(info.notes) },
        { "previousReleasesNotes", Val(releasesNotesToValList(info.previousReleasesNotes)) }
    };
}

static ReleaseInfo releaseInfoFromValMap(const ValMap& map)
{
    ReleaseInfo info;
    info.version = map.at("version").toString();
    info.fileName = map.at("fileName").toString();
    info.fileUrl = map.at("fileUrl").toString();
    info.notes = map.at("notes").toString();
    info.previousReleasesNotes = releasesNotesFromValList(map.at("previousReleasesNotes").toList());

    return info;
}

void UpdateScenario::delayedInit()
{
    if (configuration()->needCheckForUpdate() && multiInstancesProvider()->instances().size() == 1) {
        QTimer::singleShot(AUTO_CHECK_UPDATE_INTERVAL, [this]() {
            doCheckForUpdate(false);
        });
    }
}

void UpdateScenario::checkForUpdate()
{
    if (isCheckStarted()) {
        return;
    }

    doCheckForUpdate(true);
}

bool UpdateScenario::isCheckStarted() const
{
    return m_progress;
}

void UpdateScenario::doCheckForUpdate(bool manual)
{
    m_progressChannel = std::make_shared<mu::Progress>();
    m_progressChannel->started.onNotify(this, [this]() {
        m_progress = true;
    });

    m_progressChannel->finished.onReceive(this, [this, manual](const ProgressResult& res) {
        DEFER {
            m_progress = false;
        };

        if (!res.ret) {
            LOGE() << "Unable to check for update, error: " << res.ret.toString();

            if (manual) {
                processUpdateResult(res.ret.code());
            }

            return;
        }

        ReleaseInfo info = releaseInfoFromValMap(res.val.toMap());
        bool noUpdate = res.ret.code() == static_cast<int>(Err::NoUpdate);
        if (!manual) {
            bool shouldIgnoreUpdate = info.version == configuration()->skippedReleaseVersion();
            if (noUpdate || shouldIgnoreUpdate) {
                return;
            }
        } else if (noUpdate) {
            showNoUpdateMsg();
            return;
        }

        showReleaseInfo(info);
    });

    Concurrent::run(this, &UpdateScenario::th_heckForUpdate);
}

void UpdateScenario::th_heckForUpdate()
{
    m_progressChannel->started.notify();

    RetVal<ReleaseInfo> retVal = updateService()->checkForUpdate();

    RetVal<Val> result;
    result.ret = retVal.ret;
    result.val = Val(releaseInfoToValMap(retVal.val));
    m_progressChannel->finished.send(result);
}

void UpdateScenario::processUpdateResult(int errorCode)
{
    if (errorCode < static_cast<int>(Ret::Code::UpdateFirst)
        || errorCode > static_cast<int>(Ret::Code::UpdateLast)) {
        return;
    }

    Err error = static_cast<Err>(errorCode);

    switch (error) {
    case Err::NoError:
        break;
    case Err::NoUpdate:
        showNoUpdateMsg();
        break;
    default:
        showServerErrorMsg();
        break;
    }
}

void UpdateScenario::showNoUpdateMsg()
{
    QString str = mu::qtrc("update", "You already have the latest version of MuseScore. "
                                     "Please visit <a href=\"%1\">musescore.org</a> for news on what’s coming next.")
                  .arg(QString::fromStdString(configuration()->museScoreUrl()));

    IInteractive::Text text(str.toStdString(), IInteractive::TextFormat::RichText);
    IInteractive::ButtonData okBtn = interactive()->buttonData(IInteractive::Button::Ok);

    interactive()->info(mu::trc("update", "You’re up to date!"), text, { okBtn }, okBtn.btn,
                        IInteractive::Option::WithIcon);
}

void UpdateScenario::showReleaseInfo(const ReleaseInfo& info)
{
    UriQuery query("musescore://update/releaseinfo");
    query.addParam("notes", Val(info.notes));
    query.addParam("previousReleasesNotes", Val(releasesNotesToValList(info.previousReleasesNotes)));

    RetVal<Val> rv = interactive()->open(query);
    if (!rv.ret) {
        LOGD() << rv.ret.toString();
        return;
    }

    QString actionCode = rv.val.toQString();

    if (actionCode == "install") {
        downloadRelease();
    } else if (actionCode == "remindLater") {
        return;
    } else if (actionCode == "skip") {
        configuration()->setSkippedReleaseVersion(info.version);
    }
}

void UpdateScenario::showServerErrorMsg()
{
    interactive()->error(mu::trc("update", "Cannot connect to server"),
                         mu::trc("update", "Sorry - please try again later"));
}

void UpdateScenario::downloadRelease()
{
    RetVal<Val> rv = interactive()->open("musescore://update?mode=download");
    if (!rv.ret) {
        processUpdateResult(rv.ret.code());
        return;
    }

    closeAppAndStartInstallation(rv.val.toString());
}

void UpdateScenario::closeAppAndStartInstallation(const io::path_t& installerPath)
{
    std::string info = mu::trc("update", "MuseScore needs to close to complete the installation. "
                                         "If you have any unsaved changes, you will be prompted to save them before MuseScore closes.");

    int closeBtn = int(IInteractive::Button::CustomButton) + 1;
    IInteractive::Result result = interactive()->info("", info,
                                                      { interactive()->buttonData(IInteractive::Button::Cancel),
                                                        IInteractive::ButtonData(closeBtn, mu::trc("update", "Close"), true) },
                                                      closeBtn);

    if (result.standardButton() == IInteractive::Button::Cancel) {
        return;
    }

    if (multiInstancesProvider()->instances().size() != 1) {
        multiInstancesProvider()->quitAllAndRunInstallation(installerPath);
    }

    dispatcher()->dispatch("quit", ActionData::make_arg2<bool, std::string>(false, installerPath.toStdString()));
}
