#pragma once

#include <cstdint>

namespace cs2
{

// Layout verified against the game's own convar-registry walk (tier0 0x157A20, build
// 68f386a6): the CCvar's list member base = CCvar+0x48, with
//   size u16 @+0 (CCvar+0x48), allocationCount u16 @+2 (CCvar+0x4A, masked 0x7fff by the
//   walk), the node array pointer @+8 (CCvar+0x50), m_Head u16 @+16 (CCvar+0x58), and
//   16-byte nodes {ElementType m_Element @0, m_Previous u16 @8, m_Next u16 @0xA}.
// (A 2026-09-27 "fix" that moved m_Head to +2 iterated the allocation count as a head and
// crashed findConVar on a NULL element - reverted same day; the struct was never wrong.)
template <typename ElementType, typename IndexType = std::uint16_t>
struct CUtlLinkedList {
    struct UtlLinkedListElem_t {
        ElementType m_Element;
        IndexType m_Previous;
        IndexType m_Next;
    };

    static constexpr auto kInvalidIndex{static_cast<IndexType>(-1)};

    IndexType size;
    IndexType allocationCount;
    UtlLinkedListElem_t* memory;
    IndexType m_Head;
};

}
