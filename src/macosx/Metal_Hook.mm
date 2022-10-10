/*
 * Copyright (C) 2019-2020 Nemirtingas
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

#include "Metal_Hook.h"
#include "NSView_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_metal.h>

#include <objc/runtime.h>

Metal_Hook* Metal_Hook::_inst = nullptr;

decltype(Metal_Hook::DLL_NAME) Metal_Hook::DLL_NAME;


bool Metal_Hook::StartHook(std::function<void()> key_combination_callback, std::set<ingame_overlay::ToggleKey> toggle_keys, /*ImFontAtlas* */ void* imgui_font_atlas)
{
    if (!_Hooked)
    {
        //if (MTKViewDraw == nullptr)
        //{
        //    SPDLOG_WARN("Failed to hook Metal: Rendering functions missing.");
        //    return false;
        //}

        if (!NSView_Hook::Inst()->StartHook(key_combination_callback, toggle_keys))
            return false;

        SPDLOG_INFO("Hooked Metal");
        _Hooked = true;

        _ImGuiFontAtlas = imgui_font_atlas;
        
        Method ns_method;
        Class mtlcommandbuffer_class;

        // IGAccel classes hooks
        mtlcommandbuffer_class = objc_getClass("MTLIGAccelCommandBuffer");
        ns_method = class_getInstanceMethod(mtlcommandbuffer_class, @selector(renderCommandEncoderWithDescriptor:));
        
        if (ns_method != nil)
        {
            MTLIGAccelCommandBufferRenderCommandEncoderWithDescriptor = (decltype(MTLIGAccelCommandBufferRenderCommandEncoderWithDescriptor))method_setImplementation(ns_method, (IMP)&MyMTLIGAccelCommandBufferRenderCommandEncoderWithDescriptor);
        }

        mtlcommandbuffer_class = objc_getClass("MTLIGAccelRenderCommandEncoder");
        ns_method = class_getInstanceMethod(mtlcommandbuffer_class, @selector(endEncoding));
        
        if (ns_method != nil)
        {
            MTLIGAccelRenderCommandEncoderEndEncoding = (decltype(MTLIGAccelRenderCommandEncoderEndEncoding))method_setImplementation(ns_method, (IMP)&MyMTLIGAccelRenderCommandEncoderEndEncoding);
        }
    }
    return true;
}

void Metal_Hook::HideAppInputs(bool hide)
{
    if (_Initialized)
    {
        NSView_Hook::Inst()->HideAppInputs(hide);
    }
}

void Metal_Hook::HideOverlayInputs(bool hide)
{
    if (_Initialized)
    {
        NSView_Hook::Inst()->HideOverlayInputs(hide);
    }
}

bool Metal_Hook::IsStarted()
{
    return _Hooked;
}

void Metal_Hook::_ResetRenderState()
{
    if (_Initialized)
    {
        OverlayHookReady(false);

        ImGui_ImplMetal_Shutdown();
        //NSView_Hook::Inst()->_ResetRenderState();
        ImGui::DestroyContext();

        _MetalDevice = nil;
        
        _Initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void Metal_Hook::_PrepareForOverlay(render_pass_t& render_pass)
//void Metal_Hook::_PrepareForOverlay(id<MTLRenderCommandEncoder> mtl_encoder)
{
    //static id<MTLCommandQueue> commandQueue;
    if( !_Initialized )
    {
        ImGui::CreateContext(reinterpret_cast<ImFontAtlas*>(_ImGuiFontAtlas));
        
        _MetalDevice = [render_pass.command_buffer device];
        //commandQueue = [_MetalDevice newCommandQueue];

        ImGui_ImplMetal_Init(_MetalDevice);
        
        _Initialized = true;
        OverlayHookReady(true);
    }
    
    if (NSView_Hook::Inst()->PrepareForOverlay() && ImGui_ImplMetal_NewFrame(render_pass.descriptor))
    {
        //id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        //id<MTLRenderCommandEncoder> renderEncoder = MTLIGAccelCommandBufferRenderCommandEncoderWithDescriptor(commandBuffer, (SEL)"renderCommandEncoderWithDescriptor:", render_pass.descriptor);

        ImGui::NewFrame();
        
        OverlayProc();
        
        ImGui::Render();

        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), render_pass.command_buffer, render_pass.encoder);

        //ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);
        //MTLIGAccelRenderCommandEncoderEndEncoding(renderEncoder, (SEL)"endEncoding");
    }
}

id<MTLRenderCommandEncoder> Metal_Hook::MyMTLIGAccelCommandBufferRenderCommandEncoderWithDescriptor(id<MTLCommandBuffer> self, SEL sel, MTLRenderPassDescriptor* descriptor)
{
    Metal_Hook* inst = Metal_Hook::Inst();
    id<MTLRenderCommandEncoder> encoder = inst->MTLIGAccelCommandBufferRenderCommandEncoderWithDescriptor(self, sel, descriptor);
    
    inst->_RenderPass.emplace_back(render_pass_t{
        descriptor,
        self,
        encoder,
    });
    
    return encoder;
}

void Metal_Hook::MyMTLIGAccelRenderCommandEncoderEndEncoding(id<MTLRenderCommandEncoder> self, SEL sel)
{
    Metal_Hook* inst = Metal_Hook::Inst();

    //inst->MTLIGAccelRenderCommandEncoderEndEncoding(self, sel);
    for(auto it = inst->_RenderPass.begin(); it != inst->_RenderPass.end(); ++it)
    {
        if(it->encoder == self)
        {
            inst->_PrepareForOverlay(*it);
            inst->_RenderPass.erase(it);
            break;
        }
    }
    
    inst->MTLIGAccelRenderCommandEncoderEndEncoding(self, sel);
}

Metal_Hook::Metal_Hook():
    _Initialized(false),
    _Hooked(false),
    _ImGuiFontAtlas(nullptr),
    _MetalDevice(nil),
    MTLIGAccelCommandBufferRenderCommandEncoderWithDescriptor(nullptr),
    MTLIGAccelRenderCommandEncoderEndEncoding(nullptr)
{
    
}

Metal_Hook::~Metal_Hook()
{
    SPDLOG_INFO("Metal Hook removed");

    if (_Hooked)
    {
        Method ns_method;
        Class mtlcommandbuffer_class;
        
        if (MTLIGAccelCommandBufferRenderCommandEncoderWithDescriptor != nullptr)
        {
            mtlcommandbuffer_class = objc_getClass("MTLIGAccelCommandBuffer");
            ns_method = class_getInstanceMethod(mtlcommandbuffer_class, @selector(renderCommandEncoderWithDescriptor:));
            if (ns_method != nil)
                method_setImplementation(ns_method, (IMP)&MTLIGAccelCommandBufferRenderCommandEncoderWithDescriptor);

            MTLIGAccelCommandBufferRenderCommandEncoderWithDescriptor = nullptr;
        }
        if (MTLIGAccelRenderCommandEncoderEndEncoding != nullptr)
        {
            mtlcommandbuffer_class = objc_getClass("MTLIGAccelRenderCommandEncoder");
            ns_method = class_getInstanceMethod(mtlcommandbuffer_class, @selector(endEncoding));
            if (ns_method != nil)
                method_setImplementation(ns_method, (IMP)MTLIGAccelRenderCommandEncoderEndEncoding);

            MTLIGAccelRenderCommandEncoderEndEncoding = nullptr;
        }
    }

    if (_Initialized)
    {
        ImGui_ImplMetal_Shutdown();
        ImGui::DestroyContext();
        _MetalDevice = nil;
    }

    _inst = nullptr;
}

Metal_Hook* Metal_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new Metal_Hook;

    return _inst;
}

std::string Metal_Hook::GetLibraryName() const
{
    return LibraryName;
}

void Metal_Hook::LoadFunctions()
{
    
}

std::weak_ptr<uint64_t> Metal_Hook::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    return std::shared_ptr<uint64_t>();
}

void Metal_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    auto ptr = resource.lock();
    if (ptr)
    {
        auto it = _ImageResources.find(ptr);
        if (it != _ImageResources.end())
            _ImageResources.erase(it);
    }
}