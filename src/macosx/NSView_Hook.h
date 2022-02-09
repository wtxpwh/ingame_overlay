/*
 * Copyright (C) Nemirtingas
 * This file is part of the ingame overlay project
 *
 * The ingame overlay project is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 * 
 * The ingame overlay project is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the ingame overlay project; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <ingame_overlay/Renderer_Hook.h>

#include "../internal_includes.h"

#include "objc_wrappers.h"

class NSView_Hook :
    public Base_Hook
{
public:
    static constexpr const char* DLL_NAME = "AppKit";

private:
    static NSView_Hook* _inst;

    // Variables
    bool hooked;
    bool initialized;
    class ObjCHookWrapper* nsview_hook;

    // Functions
    NSView_Hook();

public:
    std::string LibraryName;

    virtual ~NSView_Hook();

    void resetRenderState();
    bool prepareForOverlay();

    std::function<bool(bool)> key_combination_callback;
    bool IgnoreInputs();
    void HandleNSEvent(void* _NSEvent, void* _NSView);

    bool start_hook(std::function<bool(bool)>& key_combination_callback);
    static NSView_Hook* Inst();
    virtual std::string GetLibraryName() const;

};
