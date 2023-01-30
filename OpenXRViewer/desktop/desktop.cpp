#include "desktop.h"

#include <limits>

void Transition(ID3D12GraphicsCommandList* renderList, ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, from, to);
    renderList->ResourceBarrier(1, &barrier);
}