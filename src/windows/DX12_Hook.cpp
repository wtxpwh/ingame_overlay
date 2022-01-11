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

#include "DX12_Hook.h"
#include "Windows_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>

DX12_Hook* DX12_Hook::_inst = nullptr;

template<typename T>
inline void SafeRelease(T*& pUnk)
{
    if (pUnk != nullptr)
    {
        pUnk->Release();
        pUnk = nullptr;
    }
}

bool DX12_Hook::start_hook(std::function<bool(bool)> key_combination_callback)
{
    if (!hooked)
    {
        if (Present == nullptr || ResizeTarget == nullptr || ResizeBuffers == nullptr || ExecuteCommandLists == nullptr)
        {
            SPDLOG_WARN("Failed to hook DirectX 12: Rendering functions missing.");
            return false;
        }

        if (!Windows_Hook::Inst()->start_hook(key_combination_callback))
            return false;

        windows_hooked = true;

        SPDLOG_INFO("Hooked DirectX 12");
        hooked = true;

        BeginHook();
        HookFuncs(
            std::make_pair<void**, void*>(&(PVOID&)Present            , &DX12_Hook::MyPresent),
            std::make_pair<void**, void*>(&(PVOID&)ResizeTarget       , &DX12_Hook::MyResizeTarget),
            std::make_pair<void**, void*>(&(PVOID&)ResizeBuffers      , &DX12_Hook::MyResizeBuffers),
            std::make_pair<void**, void*>(&(PVOID&)ExecuteCommandLists, &DX12_Hook::MyExecuteCommandLists)
        );
        EndHook();
    }
    return true;
}

bool DX12_Hook::is_started()
{
    return hooked;
}

//DX12_Hook::heap_t DX12_Hook::get_free_texture_heap()
//{
//    int64_t i;
//    std::vector<bool>::reference* free_heap;
//    for (i = 0; i < srvDescHeapBitmap.size(); ++i)
//    {
//        if (!srvDescHeapBitmap[i])
//        {
//            srvDescHeapBitmap[i] = true;
//            break;
//        }
//    }
//
//    if (i == srvDescHeapBitmap.size())
//        return heap_t{ {}, {}, -1 };
//
//    UINT inc = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
//
//    return heap_t{ 
//        pSrvDescHeap->GetGPUDescriptorHandleForHeapStart().ptr + inc * i,
//        pSrvDescHeap->GetCPUDescriptorHandleForHeapStart().ptr + inc * i,
//        i
//    };
//}
//
//bool DX12_Hook::release_texture_heap(int64_t heap_id)
//{
//    srvDescHeapBitmap[heap_id] = false;
//    return true;
//}

void DX12_Hook::resetRenderState()
{
    if (initialized)
    {
        overlay_hook_ready(false);

        ImGui_ImplDX12_Shutdown();
        Windows_Hook::Inst()->resetRenderState();
        ImGui::DestroyContext();

        for (UINT i = 0; i < bufferCount; ++i)
        {
            pCmdAlloc[i]->Release();
            pBackBuffer[i]->Release();
        }

        SafeRelease(pSrvDescHeap);
        SafeRelease(pRtvDescHeap);
        SafeRelease(pDevice);

        delete[]pCmdAlloc;
        delete[]pBackBuffer;

        initialized = false;
    }
}

// Try to make this function and overlay's proc as short as possible or it might affect game's fps.
void DX12_Hook::prepareForOverlay(IDXGISwapChain* pSwapChain)
{
    if (pCmdQueue == nullptr)
        return;

    ID3D12CommandQueue* pCmdQueue = this->pCmdQueue;

    IDXGISwapChain3* pSwapChain3 = nullptr;
    DXGI_SWAP_CHAIN_DESC sc_desc;
    pSwapChain->QueryInterface(IID_PPV_ARGS(&pSwapChain3));
    if (pSwapChain3 == nullptr)
        return;

    pSwapChain3->GetDesc(&sc_desc);

    if (!initialized)
    {
        UINT bufferIndex = pSwapChain3->GetCurrentBackBufferIndex();
        pDevice = nullptr;
        if (pSwapChain3->GetDevice(IID_PPV_ARGS(&pDevice)) != S_OK)
            return;

        bufferCount = sc_desc.BufferCount;

        mainRenderTargets.clear();
		//srvDescHeapBitmap.clear();

        //constexpr UINT descriptor_count = 1024;

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = 1;
			//desc.NumDescriptors = descriptor_count;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pSrvDescHeap)) != S_OK)
            {
                pDevice->Release();
                pSwapChain3->Release();
                return;
            }
        }
		
        //srvDescHeapBitmap.resize(descriptor_count, false);

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = bufferCount;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask = 1;
            if (pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pRtvDescHeap)) != S_OK)
            {
                pDevice->Release();
                pSwapChain3->Release();
                pSrvDescHeap->Release();
                return;
            }
        
            SIZE_T rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = pRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
            pCmdAlloc = new ID3D12CommandAllocator * [bufferCount];
            for (UINT i = 0; i < bufferCount; ++i)
            {
                mainRenderTargets.push_back(rtvHandle);
                rtvHandle.ptr += rtvDescriptorSize;
            }
        }
        
        for (UINT i = 0; i < sc_desc.BufferCount; ++i)
        {
            if (pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCmdAlloc[i])) != S_OK)
            {
                pDevice->Release();
                pSwapChain3->Release();
                pSrvDescHeap->Release();
                for (UINT j = 0; j < i; ++j)
                {
                    pCmdAlloc[j]->Release();
                }
                pRtvDescHeap->Release();
                delete[]pCmdAlloc;
                return;
            }
        }

        if (pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pCmdAlloc[0], NULL, IID_PPV_ARGS(&pCmdList)) != S_OK ||
            pCmdList->Close() != S_OK)
        {
            pDevice->Release();
            pSwapChain3->Release();
            pSrvDescHeap->Release();
            for (UINT i = 0; i < bufferCount; ++i)
                pCmdAlloc[i]->Release();
            pRtvDescHeap->Release();
            delete[]pCmdAlloc;
            return;
        }

        pBackBuffer = new ID3D12Resource * [bufferCount];
        for (UINT i = 0; i < bufferCount; i++)
        {
            pSwapChain3->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer[i]));
            pDevice->CreateRenderTargetView(pBackBuffer[i], NULL, mainRenderTargets[i]);
        }

        //auto heaps = std::move(get_free_texture_heap());

        ImGui::CreateContext();
        ImGui_ImplDX12_Init(pDevice, bufferCount, DXGI_FORMAT_R8G8B8A8_UNORM, pSrvDescHeap,
            pSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
            pSrvDescHeap->GetGPUDescriptorHandleForHeapStart());
			//heaps.cpu_handle,
            //heaps.gpu_handle);
        
        initialized = true;
        overlay_hook_ready(true);
    }

    if (ImGui_ImplDX12_NewFrame() && Windows_Hook::Inst()->prepareForOverlay(sc_desc.OutputWindow))
    {
        ImGui::NewFrame();

        overlay_proc();

        UINT bufferIndex = pSwapChain3->GetCurrentBackBufferIndex();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = pBackBuffer[bufferIndex];
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        pCmdAlloc[bufferIndex]->Reset();
        pCmdList->Reset(pCmdAlloc[bufferIndex], NULL);
        pCmdList->ResourceBarrier(1, &barrier);
        pCmdList->OMSetRenderTargets(1, &mainRenderTargets[bufferIndex], FALSE, NULL);
        pCmdList->SetDescriptorHeaps(1, &pSrvDescHeap);

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCmdList);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        pCmdList->ResourceBarrier(1, &barrier);
        pCmdList->Close();

        pCmdQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&pCmdList);
    }
    pSwapChain3->Release();
}

HRESULT STDMETHODCALLTYPE DX12_Hook::MyPresent(IDXGISwapChain *_this, UINT SyncInterval, UINT Flags)
{
    auto inst = DX12_Hook::Inst();
    inst->prepareForOverlay(_this);
    return (_this->*inst->Present)(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DX12_Hook::MyResizeTarget(IDXGISwapChain* _this, const DXGI_MODE_DESC* pNewTargetParameters)
{
    auto inst = DX12_Hook::Inst();
    inst->resetRenderState();
    return (_this->*inst->ResizeTarget)(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE DX12_Hook::MyResizeBuffers(IDXGISwapChain* _this, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    auto inst = DX12_Hook::Inst();
    inst->resetRenderState();
    return (_this->*inst->ResizeBuffers)(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

void STDMETHODCALLTYPE DX12_Hook::MyExecuteCommandLists(ID3D12CommandQueue *_this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists)
{
    auto inst = DX12_Hook::Inst();
    inst->pCmdQueue = _this;
    (_this->*inst->ExecuteCommandLists)(NumCommandLists, ppCommandLists);
}

DX12_Hook::DX12_Hook():
    initialized(false),
    pCmdQueue(nullptr),
    bufferCount(0),
    pCmdAlloc(nullptr),
    pSrvDescHeap(nullptr),
    pCmdList(nullptr),
    pRtvDescHeap(nullptr),
    hooked(false),
    windows_hooked(false),
    Present(nullptr),
    ResizeBuffers(nullptr),
    ResizeTarget(nullptr),
    ExecuteCommandLists(nullptr)
{
    SPDLOG_WARN("DX12 support is experimental, don't complain if it doesn't work as expected.");
}

DX12_Hook::~DX12_Hook()
{
    SPDLOG_INFO("DX12 Hook removed");

    if (windows_hooked)
        delete Windows_Hook::Inst();

    if (initialized)
    {
        pSrvDescHeap->Release();
        for (UINT i = 0; i < bufferCount; ++i)
        {
            pCmdAlloc[i]->Release();
            pBackBuffer[i]->Release();
        }
        pRtvDescHeap->Release();
        delete[]pCmdAlloc;
        delete[]pBackBuffer;

        ImGui_ImplDX12_InvalidateDeviceObjects();
        ImGui::DestroyContext();

        initialized = false;
    }

    _inst = nullptr;
}

DX12_Hook* DX12_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new DX12_Hook();

    return _inst;
}

std::string DX12_Hook::GetLibraryName() const
{
    return LibraryName;
}

void DX12_Hook::loadFunctions(decltype(Present) PresentFcn, decltype(ResizeBuffers) ResizeBuffersFcn, decltype(ResizeTarget) ResizeTargetFcn, decltype(ExecuteCommandLists) ExecuteCommandListsFcn)
{
    Present = PresentFcn;
    ResizeBuffers = ResizeBuffersFcn;
    ResizeTarget = ResizeTargetFcn;

    ExecuteCommandLists = ExecuteCommandListsFcn;
}

std::weak_ptr<uint64_t> DX12_Hook::CreateImageResource(const void* image_data, uint32_t width, uint32_t height)
{
    return std::shared_ptr<uint64_t>();
	//heap_t heap = get_free_texture_heap();
	//
    //if (heap.id == -1)
    //    return nullptr;
	//
    //HRESULT hr;
	//
    //D3D12_HEAP_PROPERTIES props;
    //memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
    //props.Type = D3D12_HEAP_TYPE_DEFAULT;
    //props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    //props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	//
    //D3D12_RESOURCE_DESC desc;
    //ZeroMemory(&desc, sizeof(desc));
    //desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    //desc.Alignment = 0;
    //desc.Width = source->width();
    //desc.Height = source->height();
    //desc.DepthOrArraySize = 1;
    //desc.MipLevels = 1;
    //desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    //desc.SampleDesc.Count = 1;
    //desc.SampleDesc.Quality = 0;
    //desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    //desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	//
    //ID3D12Resource* pTexture = NULL;
    //pDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
    //    D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(&pTexture));
	//
    //UINT uploadPitch = (source->width() * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
    //UINT uploadSize = source->height() * uploadPitch;
    //desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    //desc.Alignment = 0;
    //desc.Width = uploadSize;
    //desc.Height = 1;
    //desc.DepthOrArraySize = 1;
    //desc.MipLevels = 1;
    //desc.Format = DXGI_FORMAT_UNKNOWN;
    //desc.SampleDesc.Count = 1;
    //desc.SampleDesc.Quality = 0;
    //desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    //desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	//
    //props.Type = D3D12_HEAP_TYPE_UPLOAD;
    //props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    //props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	//
    //ID3D12Resource* uploadBuffer = NULL;
    //hr = pDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
    //    D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&uploadBuffer));
    //IM_ASSERT(SUCCEEDED(hr));
	//
    //void* mapped = NULL;
    //D3D12_RANGE range = { 0, uploadSize };
    //hr = uploadBuffer->Map(0, &range, &mapped);
    //IM_ASSERT(SUCCEEDED(hr));
    //for (int y = 0; y < source->height(); y++)
    //    memcpy((void*)((uintptr_t)mapped + y * uploadPitch), reinterpret_cast<uint8_t*>(source->get_raw_pointer()) + y * source->width() * 4, source->width() * 4);
    //uploadBuffer->Unmap(0, &range);
	//
    //D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    //srcLocation.pResource = uploadBuffer;
    //srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    //srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    //srcLocation.PlacedFootprint.Footprint.Width = source->width();
    //srcLocation.PlacedFootprint.Footprint.Height = source->height();
    //srcLocation.PlacedFootprint.Footprint.Depth = 1;
    //srcLocation.PlacedFootprint.Footprint.RowPitch = uploadPitch;
	//
    //D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    //dstLocation.pResource = pTexture;
    //dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    //dstLocation.SubresourceIndex = 0;
	//
    //D3D12_RESOURCE_BARRIER barrier = {};
    //barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    //barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    //barrier.Transition.pResource = pTexture;
    //barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    //barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    //barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	//
    //ID3D12Fence* fence = NULL;
    //hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    //IM_ASSERT(SUCCEEDED(hr));
	//
    //HANDLE event = CreateEvent(0, 0, 0, 0);
    //IM_ASSERT(event != NULL);
	//
    //D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    //queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    //queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    //queueDesc.NodeMask = 1;
	//
    //ID3D12CommandQueue* cmdQueue = NULL;
    //hr = pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue));
    //IM_ASSERT(SUCCEEDED(hr));
	//
    //ID3D12CommandAllocator* cmdAlloc = NULL;
    //hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    //IM_ASSERT(SUCCEEDED(hr));
	//
    //ID3D12GraphicsCommandList* cmdList = NULL;
    //hr = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, NULL, IID_PPV_ARGS(&cmdList));
    //IM_ASSERT(SUCCEEDED(hr));
	//
    //cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, NULL);
    //cmdList->ResourceBarrier(1, &barrier);
	//
    //hr = cmdList->Close();
    //IM_ASSERT(SUCCEEDED(hr));
	//
    //cmdQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&cmdList);
    //hr = cmdQueue->Signal(fence, 1);
    //IM_ASSERT(SUCCEEDED(hr));
	//
    //fence->SetEventOnCompletion(1, event);
    //WaitForSingleObject(event, INFINITE);
	//
    //cmdList->Release();
    //cmdAlloc->Release();
    //cmdQueue->Release();
    //CloseHandle(event);
    //fence->Release();
    //uploadBuffer->Release();
	//
    //// Create texture view
    //D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    //ZeroMemory(&srvDesc, sizeof(srvDesc));
    //srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    //srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    //srvDesc.Texture2D.MipLevels = desc.MipLevels;
    //srvDesc.Texture2D.MostDetailedMip = 0;
    //srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	//
    //pDevice->CreateShaderResourceView(pTexture, &srvDesc, heap.cpu_handle);
    //
    ////pSrvDescHeap->Release();
    ////pTexture->Release();
	//
    //using gpu_heap_t = decltype(D3D12_GPU_DESCRIPTOR_HANDLE::ptr);
    //struct texture_t{
    //    gpu_heap_t gpu_handle; // This must be the first member, ImGui will use the content of the pointer as a D3D12_GPU_DESCRIPTOR_HANDLE::ptr
    //    ID3D12Resource* pTexture;
    //    int64_t heap_id;
    //};
	//
    //texture_t* texture_data = new texture_t;
    //texture_data->gpu_handle = heap.gpu_handle.ptr;
    //texture_data->pTexture = pTexture;
    //texture_data->heap_id = heap.id;
	//
    //return std::shared_ptr<uint64_t>((uint64_t*)texture_data, [this](uint64_t* handle)
    //{
    //    if (handle != nullptr)
    //    {
    //        texture_t* pTextureData = reinterpret_cast<texture_t*>(handle);
    //        pTextureData->pTexture->Release();
    //        release_texture_heap(pTextureData->heap_id);
	//
    //        delete pTextureData;
    //    }
    //});
}

void DX12_Hook::ReleaseImageResource(std::weak_ptr<uint64_t> resource)
{
    //auto ptr = resource.lock();
    //if (ptr)
    //{
    //    auto it = image_resources.find(ptr);
    //    if (it != image_resources.end())
    //        image_resources.erase(it);
    //}
}