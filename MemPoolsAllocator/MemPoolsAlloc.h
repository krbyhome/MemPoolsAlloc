#pragma once

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <utility>
#include <iostream>
#include <vector>

const size_t kByte = 8;
std::vector<std::pair<size_t, size_t>> config;

namespace PoolAlloc {

    void set_config(const std::vector<std::pair<size_t, size_t>>& cfg) {
        config = cfg;
    }

    class Pool {
    public:
        const size_t block_size;
        const size_t block_counter;
        size_t blocks_free_num;
        size_t free_block;

        Pool(size_t block_size_, size_t block_counter_);

        ~Pool();

        bool find(void* pointer_) const;

        void* allocate(size_t bytes_);
        void deallocate(void* pointer_, size_t bytes_);

        Pool* next_pool = nullptr;
        Pool* prev_pool = nullptr;

    private:
        size_t get_blocks(size_t n);
        void use_blocks(size_t idx_, size_t n);
        void free_blocks(size_t idx_, size_t n);

        char* mem_pointer = nullptr;
        char* used_blocks = nullptr;
    };

    bool Pool::find(void* pointer_) const {
        return mem_pointer <= pointer_ && pointer_ < mem_pointer + block_size * block_counter;
    }

    size_t Pool::get_blocks(size_t n) {
        bool ok = true;
        size_t left = 0;
        size_t right = 0;
        //GETTING N BLOCKS
        for (size_t i = free_block; i + n <= block_counter; ++i) {
            left = i;
            right = i;
            size_t cur_block_idx;
            bool in_use_right;
            while (right - left + 1 <= n && ok) {
                cur_block_idx = right / kByte;
                in_use_right = (used_blocks[cur_block_idx] >> (kByte - 1 - right % kByte)) % 2;
                //SET BLOCK IN_USE_RIGHT IN USE
                if (in_use_right) {
                    i = right;
                    ok = false;
                }
                ++right;
            }
            --right;
            if (right - left + 1 == n && ok) {
                if (i == free_block)
                    free_block = left + n;

                return left;
            }
            ok = true;
        }

        return block_counter;
    }

    void Pool::use_blocks(size_t idx_, size_t n) {
        for (size_t i = idx_; i < idx_ + n; ++i) {
            size_t cur_block_idx = i / kByte;
            size_t order = (kByte - 1 - i % kByte);
            assert( (used_blocks[cur_block_idx] >> order ) % 2 != 1);

            char mask = 1 << order;
            used_blocks[cur_block_idx] = used_blocks[cur_block_idx] | mask;
            --blocks_free_num;
        }
    }

    void Pool::free_blocks(size_t idx_, size_t n) {
        for (size_t i = idx_; i < idx_ + n; ++i) {
            size_t cur_block_idx = i / kByte;
            size_t order = (kByte - 1 - i % kByte);
            uint8_t mask = {0b1111'1111};
            uint8_t mask_2 = 1 << order;
            mask = mask_2 ^ mask;
            used_blocks[cur_block_idx] = used_blocks[cur_block_idx] & mask;
        }
        if (idx_ < free_block) {
            free_block = idx_;
        }
        blocks_free_num += n;
    }

    void* Pool::allocate(size_t bytes_) {
        size_t number_of_blocks = 1 + ((bytes_ - 1) / block_size);
        size_t idx_ = get_blocks(number_of_blocks);
        //IDX_ IS INDEX OF FIRST BLOCK THAT WILL BE ALLOCATED

        if (idx_ >= block_counter) {
            return nullptr;
        }

        use_blocks(idx_, number_of_blocks);

        return mem_pointer + idx_ * block_size;
    }

    void Pool::deallocate(void* pointer_, size_t bytes_) {
        if (pointer_ == nullptr) {
            throw std::bad_alloc{};
        }

        size_t blocks_to_deallocate = 1 + (bytes_ - 1) / block_size;
        char* pointer_to_data = static_cast<char*>(pointer_);
        size_t d = std::distance(mem_pointer, pointer_to_data);
        size_t block_number = d / block_size;

        free_blocks(block_number, blocks_to_deallocate);
    }

    Pool::Pool(size_t block_size_, size_t block_counter_)
            : block_size(block_size_),
              block_counter(block_counter_),
              blocks_free_num(block_counter_),
              free_block(0){
        size_t data_size = block_size * block_counter;
        mem_pointer = static_cast<char*>(std::malloc(data_size));
        assert(mem_pointer != nullptr);
        std::fill(mem_pointer, mem_pointer + data_size, 0);

        size_t used_blocks_size = 1 + ((block_counter - 1) / kByte);
        used_blocks = static_cast<char*>(std::malloc(used_blocks_size));
        assert(used_blocks != nullptr);
        std::fill(used_blocks, used_blocks + used_blocks_size, 0);

    }

    Pool::~Pool() {
        delete mem_pointer;
        delete used_blocks;
    }

    class MemPoolList {
    public:
        Pool* head;
        Pool* tail;

        MemPoolList(std::vector<std::pair<size_t, size_t>>& list_) {
            head = new Pool(list_.begin()->first, list_.begin()->second);
            if (list_.size() > 1) {
                tail = new Pool(list_.back().first, list_.back().second);
            } else {
                tail = head;
            }
            size_t counter = 0;
            Pool* prev = head;
            Pool* last = head;
            for (auto pool_cfg: list_) {
                if (counter != 0 && counter != list_.size() - 1) {
                    Pool* current = new Pool(pool_cfg.first, pool_cfg.second);
                    prev->next_pool = current;
                    current->prev_pool = prev;
                    last = current;
                    prev = current;
                }
                counter++;
            }
            if (list_.size() > 1) {
                last->next_pool = tail;
                tail->prev_pool = last;
            }
        }

        ~MemPoolList() {
            while (head != nullptr) {
                Pool* cur = head;
                head = head->next_pool;
                delete cur;
            }
        }

        void* allocate(size_t bytes) {
            auto pool = head;
            while (pool != nullptr) {
                if (pool -> blocks_free_num * pool -> block_size >= bytes && pool -> blocks_free_num) {
                    //IF BLOCK FITS
                    auto pointer = pool->allocate(bytes);
                    if (pointer) {
                        return pointer;
                    }
                } //IF BLOCK DOESN'T FIT GO TO ANOTHER
                pool = pool->next_pool;
            }

            throw std::bad_alloc{};
        }

        void deallocate(void* pointer, size_t bytes) {
            auto pool = head;
            while (pool != nullptr) {
                if (pool->find(pointer)) {
                    pool->deallocate(pointer, bytes);

                    return;
                }
                pool = pool -> next_pool;
            }
        }
    };

    template <typename T>
    class Allocator_ {
    public:
        using value_type = T;
        using pointer = value_type*;
        using size_type = size_t;


        Allocator_(std::vector<std::pair<size_t, size_t>> list_)
                : memory_list(list_){ }

        Allocator_() : memory_list(config){ }

        pointer allocate(size_type n) {
            return reinterpret_cast<pointer>(memory_list.allocate(n * sizeof(T)));
        }

        void deallocate(pointer p, size_type n) {
            memory_list.deallocate(p, n * sizeof(T));
        }

    private:
        MemPoolList memory_list;
    };
};