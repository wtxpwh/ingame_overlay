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

#include <d3d12.h>
#include <dxgi1_4.h>

class DX12_Hook : 
    public Renderer_Hook,
    public Base_Hook
{
public:
    static constexpr const char *DLL_NAME = "d3d12.dll";

private:
    static DX12_Hook* _inst;

    // Variables
    bool hooked;
    bool windows_hooked;
    bool initialized;

    ID3D12CommandQueue* pCmdQueue;
    UINT bufferCount;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> mainRenderTargets;
    ID3D12Device* pDevice;
    ID3D12CommandAllocator** pCmdAlloc;
	//std::vector<bool> srvDescHeapBitmap;
    ID3D12DescriptorHeap* pSrvDescHeap;
    ID3D12GraphicsCommandList* pCmdList;
    ID3D12DescriptorHeap* pRtvDescHeap;
    ID3D12Resource** pBackBuffer;

    // Functions
    DX12_Hook();

    //struct heap_t
    //{
    //    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    //    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    //    int64_t id;
    //};
	//
    //heap_t get_free_texture_heap();
    //bool release_texture_heap(int64_t heap_id);


    void resetRenderState();
    void prepareForOverlay(IDXGISwapChain* pSwapChain);

    // Hook to render functions
    static HRESULT STDMETHODCALLTYPE MyPresent(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags);
    static HRESULT STDMETHODCALLTYPE MyResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters);
    static HRESULT STDMETHODCALLTYPE MyResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static void STDMETHODCALLTYPE MyExecuteCommandLists(ID3D12CommandQueue *_this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);

    decltype(&IDXGISwapChain::Present)       Present;
    decltype(&IDXGISwapChain::ResizeBuffers) ResizeBuffers;
    decltype(&IDXGISwapChain::ResizeTarget)  ResizeTarget;
    decltype(&ID3D12CommandQueue::ExecuteCommandLists) ExecuteCommandLists;

public:
    std::string LibraryName;

    virtual ~DX12_Hook();

    virtual bool start_hook(std::function<bool(bool)> key_combination_callback);
    virtual bool is_started();
    static DX12_Hook* Inst();
    virtual std::string GetLibraryName() const;

    void loadFunctions(decltype(Present) PresentFcn, decltype(ResizeBuffers) ResizeBuffersFcn, decltype(ResizeTarget) ResizeTargetFcn, decltype(ExecuteCommandLists) ExecuteCommandListsFcn);

    virtual std::weak_ptr<uint64_t> CreateImageResource(const void* image_data, uint32_t width, uint32_t height);
    virtual void ReleaseImageResource(std::weak_ptr<uint64_t> resource);
};
