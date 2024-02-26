/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2023 MuseScore BVBA and others
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

#ifndef MU_AUDIO_AUDIOUTILS_H
#define MU_AUDIO_AUDIOUTILS_H

#include "audiotypes.h"
#include "soundfonttypes.h"

namespace mu::audio {
inline AudioResourceMeta makeReverbMeta()
{
    AudioResourceMeta meta;
    meta.id = MUSE_REVERB_ID;
    meta.type = AudioResourceType::MusePlugin;
    meta.vendor = "Muse";
    meta.hasNativeEditorSupport = true;

    return meta;
}

inline AudioPluginType audioPluginTypeFromCategoriesString(const String& categoriesStr)
{
    static const std::vector<std::pair<String, AudioPluginType> > STRING_TO_PLUGIN_TYPE_LIST = {
        { u"Instrument", AudioPluginType::Instrument },
        { u"Fx", AudioPluginType::Fx },
    };

    for (auto it = STRING_TO_PLUGIN_TYPE_LIST.cbegin(); it != STRING_TO_PLUGIN_TYPE_LIST.cend(); ++it) {
        if (categoriesStr.contains(it->first)) {
            return it->second;
        }
    }

    return AudioPluginType::Undefined;
}

inline String audioSourceName(const AudioInputParams& params)
{
    if (params.type() == mu::audio::AudioSourceType::MuseSampler) {
        return params.resourceMeta.attributeVal(u"museName");
    }

    if (params.resourceMeta.type == audio::AudioResourceType::FluidSoundfont) {
        const String& presetName = params.resourceMeta.attributeVal(synth::PRESET_NAME_ATTRIBUTE);
        if (!presetName.empty()) {
            return presetName;
        }

        const String& soundFontName = params.resourceMeta.attributeVal(synth::SOUNDFONT_NAME_ATTRIBUTE);
        if (!soundFontName.empty()) {
            return soundFontName;
        }
    }

    return String::fromStdString(params.resourceMeta.id);
}
}

#endif // MU_AUDIO_AUDIOUTILS_H
