#pragma once

#include <cstdint>
#include <iterator>
#include <cstddef>
#include <assert.h>

inline size_t Align(size_t value, size_t alignment);

// Custom allocation, still figuring out how to use this best.
// WARNING: Anything allocated inside a memory arena won't get it's desctructor called (intentionally).
// Don't store std::string or similar in here!
class MemoryArena
{
public:
    const size_t capacity = 0;
    size_t allocationGranularity = 1024 * 64;

    uint8_t* base;
    size_t used = 0;
    size_t committed = 0;

    MemoryArena(size_t capacity = 1024 * 1024 * 1024);
    void* Allocate(size_t size);
    void Reset(bool freePages = false);

    // Copying this thing is probably a very bad idea (and moving it shouldn't be necessary).
    MemoryArena(const MemoryArena& other) = delete;
    MemoryArena(MemoryArena&& other) noexcept = delete;
    MemoryArena& operator=(const MemoryArena& other) = delete;
    MemoryArena& operator=(MemoryArena&& other) noexcept = delete;
    ~MemoryArena();
};

template <typename T>
class TypedMemoryArena : public MemoryArena
{
public:
    struct Iterator
    {
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = T*;
        using reference = T&;

        Iterator(pointer ptr) : m_ptr(ptr) {}

        reference operator*() const { return *m_ptr; }
        pointer operator->() { return m_ptr; }
        Iterator& operator++() { m_ptr++; return *this; }
        Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }
        friend bool operator== (const Iterator& a, const Iterator& b) { return a.m_ptr == b.m_ptr; };
        friend bool operator!= (const Iterator& a, const Iterator& b) { return a.m_ptr != b.m_ptr; };

    private:
        pointer m_ptr;
    };

    Iterator begin() { return Iterator(reinterpret_cast<T*>(base)); }
    Iterator end() { return Iterator(reinterpret_cast<T*>(base + used)); }
};

#define NewObject(arena, type, ...) new((arena).Allocate(sizeof(type))) type(__VA_ARGS__)
#define NewArray(arena, type, count, ...) new((arena).Allocate(sizeof(type) * (count))) type[count](__VA_ARGS__)

template <typename T>
class ArenaList
{
public:
    void Allocate(MemoryArena& arena, size_t capacity)
    {
		assert(this->base == nullptr);
		this->base = NewArray(arena, T, capacity);
        this->capacity = capacity;
        this->size = 0;
    }

    T& operator[](size_t index)
    {
        assert(index >= 0);
		assert(index < size);
        return base[index];
    }
    
    T* new_element()
    {
		assert(size < capacity);
		return &base[size++];
    }

    void clear()
    {
		size = 0;
    }

    T* base = nullptr;
    size_t capacity = 0;
    size_t size = 0;

    struct Iterator
    {
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = T*;
        using reference = T&;

        Iterator(pointer ptr) : m_ptr(ptr) {}

        reference operator*() const { return *m_ptr; }
        pointer operator->() { return m_ptr; }
        Iterator& operator++() { m_ptr++; return *this; }
        Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }
        friend bool operator== (const Iterator& a, const Iterator& b) { return a.m_ptr == b.m_ptr; };
        friend bool operator!= (const Iterator& a, const Iterator& b) { return a.m_ptr != b.m_ptr; };

    private:
        pointer m_ptr;
    };

    Iterator begin() { return Iterator(reinterpret_cast<T*>(base)); }
    Iterator end() { return Iterator(reinterpret_cast<T*>(base + size)); }
};