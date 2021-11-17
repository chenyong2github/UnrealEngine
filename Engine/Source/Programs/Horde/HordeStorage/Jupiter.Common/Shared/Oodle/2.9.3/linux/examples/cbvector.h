#pragma once

#ifndef CB_VECTOR_H_INCLUDED
#define CB_VECTOR_H_INCLUDED

/***********************


This license applies to all software available on cbloom.com unless otherwise stated.

Copyright (c) 1998-2020, Charles Bloom

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.

-------------------------------------------

cbvector.h

Single file self-contained vector implementation.

This header defined a class name CB_VECTOR in namespace CB_VECTOR_NAMESPACE.
If you don't #define those macros, they will default to "vector" and "", respectively.

cbvector.h was written by Charles Bloom.
Public domain.
Don't be a jerk.

---------------------------

This code was ripped out of cblib/vector_flex and not cleaned up.  It's ugly.

The use of size_t/intptr_t/ptrdiff_t is often wrong and always ugly.

The split into vector & vector_base is pointless obfuscation in this context.

---------------------------

Currently tested on these compilers :

MSVC 2005
GCC 4.1.1 (PS3)

***************************/

// options :

// class name :
#ifndef CB_VECTOR
#define CB_VECTOR   vector
#endif

// var type for m_size :
#ifndef CB_SIZETYPE
#define CB_SIZETYPE size_t
#endif

// assert :
#ifndef CB_ASSERT
#define CB_ASSERT(exp)
#endif

// compiler assert :
#ifndef CB_COMPILER_ASSERT
#define CB_COMPILER_ASSERT(exp)
#endif

// assert used for the vector extend malloc :
#ifndef CB_ASSERT_MALLOC
#define CB_ASSERT_MALLOC(exp)       do { if ( ! (exp) ) exit(20); } while(0)
#endif

// alloc memory :
#ifndef CB_ALLOC
#define CB_ALLOC(size)  malloc(size)
#endif

// free mem :
#ifndef CB_FREE
#define CB_FREE(ptr,size)   free(ptr)
#endif

// min :
#ifndef CB_MIN
#define CB_MIN(a,b) ( (a) < (b) ? (a) : (b) )
#endif

// max :
#ifndef CB_MAX
#define CB_MAX(a,b) ( (a) > (b) ? (a) : (b) )
#endif

// option : use swap instead of copy when extending vector
//#define CB_VECTOR_USE_SWAP

// option : set max grow size : (otherwise doubles)
// you can define CB_VECTOR_MAX_GROW_BYTES yourself before including vector.h if you like
#ifdef CB_VECTOR_MAX_GROW_BYTES
#if CB_VECTOR_MAX_GROW_BYTES == 0
// it was defined but not a valid value, set default :
#define CB_VECTOR_MAX_GROW_BYTES    (1024*1024)
#endif  //CB_VECTOR_MAX_GROW_BYTES
#endif

// namespace :
//  blank namespace ensures that includes with different options don't conflict in the linker
// define CB_VECTOR_NO_NAMESPACE if you really want none (not recommended)
#ifndef CB_VECTOR_NO_NAMESPACE
#ifndef CB_VECTOR_NAMESPACE
#define CB_VECTOR_NAMESPACE     // blank
#endif
#endif

//#include <new> // need this to get placement new

//=======================================================

#ifdef _MSC_VER
#define CB_VECTOR_FORCEINLINE __forceinline
#elif defined(__GNUC__)
#define CB_VECTOR_FORCEINLINE inline __attribute((always_inline))
#else
#define CB_VECTOR_FORCEINLINE
#endif

//=======================================================

#ifdef CB_VECTOR_NAMESPACE
namespace CB_VECTOR_NAMESPACE
{
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4345)
//d:\Exoddus\Code\Engine\Core\cb_entry_array.h(83) : warning C4345: behavior change: an object of POD type constructed with an initializer of the form () will be default-initialized
#pragma warning(disable : 4100) // unreferenced formal parameter
#pragma warning(disable : 4127) // conditional on constant
#pragma warning(disable : 4328) // indirection alignment of formal parameter parameter_number (alignment_value_for_parameter) is greater than the actual argument
#endif

//#pragma pack(push)
//#pragma pack(4)

namespace cb_entry_array
{
    /*******************

    cb_entry_array

    This is a set of helper template functions for arrays of a templated type.
    It's interesting in a few ways.  

    *******************/

    //-----------------------------------------------------------------------------------------------

    // EntryCopy
    //  like memcpy
    //  pTo and pFm must not overlap
    template <typename t_entry>
    static inline void copy(t_entry * pTo,const t_entry * pFm,const size_t count)
    {
        // assert we don't overlap in a bad way :
        CB_ASSERT( pTo != pFm || count == 0 );
        CB_ASSERT( pTo < pFm || pTo >= pFm + count );

        for(size_t i=0;i<count;i++)
        {
            pTo[i] = pFm[i];
        }
    }

    //-----------------------------------------------------------------------------------------------

    // EntryMove
    //  like memmove
    //  pTo and pFm may overlap
    template <typename t_entry>
    static inline void move(t_entry * pTo,const t_entry * pFm,const size_t count)
    {
        CB_ASSERT( pTo != pFm || count == 0 );
        
        if ( pTo > pFm )
        {
            // go backwards
            for(intptr_t i = count-1; i>= 0;i--)
            {
                pTo[i] = pFm[i];
            }
        }
        else // ( pTo < pFm )
        {
            // go forwards
            copy(pTo,pFm,count);
        }
    }

    //-----------------------------------------------------------------------------------------------

    template <typename t_entry>
    static inline void construct(t_entry * pEntry)
    {
        new (pEntry) t_entry();
    }

    //-----------------------------------------------------------------------------------------------

    template <typename t_entry>
    static inline void destruct(t_entry * pEntry)
    {
        CB_ASSERT(pEntry);

        pEntry->~t_entry();
    }
    
    //-----------------------------------------------------------------------------------------------

    template <typename t_entry>
    static inline void construct(t_entry * pArray,const size_t size)
    {
        CB_ASSERT(pArray);
        // placement new an array :
        for(size_t i=0;i<size;i++)
        {
            //new (ePlacementNew, pArray+i) t_entry();
            new (pArray+i) t_entry();
        }
    }

    //-----------------------------------------------------------------------------------------------

    template <typename t_entry>
    static inline void destruct(t_entry * pArray,const size_t size)
    {
        for(size_t i=0;i<size;i++)
        {
            CB_ASSERT(pArray);
            destruct(pArray+i);
        }
    }

    //-----------------------------------------------------------------------------------------------
    
    template <typename t_entry>
    static inline void copy_construct(t_entry * pTo,const t_entry & from)
    {
        CB_ASSERT(pTo);

        //new (ePlacementNew, pTo) t_entry(from);
        new (pTo) t_entry(from);
    }
        
    //-----------------------------------------------------------------------------------------------
    
    template <typename t_entry>
    static inline void copy_construct(t_entry * pArray,const t_entry * pFrom,const size_t size)
    {
        // placement new an array :
        for(size_t i=0;i<size;i++)
        {
            CB_ASSERT( pArray && pFrom );
            copy_construct(pArray+i,pFrom[i]);
        }
    }
    
    //-----------------------------------------------------------------------------------------------
    // not used but nice :

    /*
    // Entryswap
    //  like memcpy
    //  pArray1 and pArray2 must not overlap
    template <class t_entry>
    static inline void swap_array(t_entry * pArray1,t_entry * pArray2,const size_t count)
    {
        // assert we don't overlap in a bad way :
        CB_ASSERT( pArray1 != pArray2 );
        CB_ASSERT( pArray1 < pArray2 || pArray1 >= pArray2 + count );

        for(size_t i=0;i<count;i++)
        {
            CB_ASSERT( pArray1 && pArray2 );
            Swap(pArray1[i],pArray2[i]);
        }
    }
    */

    //-----------------------------------------------------------------------------------------------

    template <typename t_entry>
    static inline void swap_construct(t_entry * pTo,t_entry & from)
    {
        CB_ASSERT(pTo);

        new (pTo) t_entry();
        Swap(from,*pTo);
    }
    
    template <typename t_entry>
    static inline void swap_construct(t_entry * pArray,t_entry * pFrom,const size_t size)
    {
        // placement new an array :
        for(intptr_t i=0;i<size;i++)
        {
            CB_ASSERT( pArray && pFrom );
            swap_construct(pArray+i,pFrom[i]);
        }
    }
        
    //-----------------------------------------------------------------------------------------------
    
    // We need this in vector, but I don't want to include <memory> there.
    //  Note that this is slightly less flexible than the stl version,
    //  it only works with pointer iterators, not general iterators
    // has [FROM,TO] ordering unlike everything else
    template <typename t_entry>
    inline t_entry * uninitialized_copy(const t_entry*  first,
                                            const t_entry*  last,
                                            t_entry* result)
    {
        while (first != last)
        {
            new (result) t_entry(*first);
            first++;
            result++;
        }

        return result;
    }
    
    //-----------------------------------------------------------------------------------------------
};

//}{=======================================================================================
// cb_vector_base

template <typename t_entry> class cb_vector_base
{
public:

    typedef cb_vector_base<t_entry> this_type;
    typedef CB_SIZETYPE             size_type;

    //-----------------------------------------
    // constructors :

    CB_VECTOR_FORCEINLINE cb_vector_base()
    {
        init();
    }

    CB_VECTOR_FORCEINLINE ~cb_vector_base()
    {
        release();
    }

    cb_vector_base(const cb_vector_base & other)
    {
        init();
        extend_copy(other.begin(),other.size());
    }

    template <class input_iterator>
    cb_vector_base(const input_iterator first, const input_iterator last)
    {
        init();
        assign_construct(first,last);
    }

    //-----------------------------------------

    // increase size and construct added entries with their default constructor
    void extend_default(const size_type count)
    {
        const size_type oldsize = m_size;
        if ( needmakefit(m_size+count) )
        {
            const size_type oldcapacity = capacity();
            makefit2( makefit1(m_size + count) , m_size , oldcapacity);
        }
        m_size += count;
        cb_entry_array::construct(begin() + oldsize,count);
    }

    // increase size and don't construct!!!!
    void extend_no_construct(const size_type count)
    {
        if ( needmakefit(m_size+count) )
        {
            const size_type oldcapacity = capacity();
            makefit2( makefit1(m_size + count) , m_size , oldcapacity);
        }
        m_size += count;
    }
    
    // increase size and construct added entries with their copy constructor
    // this must work fine if pFrom is pointing into myself
    //  the key is that makefit1() doesn't free the old memory, so we must
    //  do any work between makefit1 and makefit2
    void extend_copy(const t_entry * pFrom,const size_type count)
    {
        if ( needmakefit(m_size+count) )
        {
            const size_type oldsize = m_size;
            const size_type oldcapacity = capacity();
            t_entry * pOld = makefit1(m_size + count);
            cb_entry_array::copy_construct(begin() + m_size,pFrom,count);
            m_size += count;
            makefit2(pOld, oldsize, oldcapacity);
        }
        else
        {
            cb_entry_array::copy_construct(begin() + m_size,pFrom,count);
            m_size += count;
        }
    }

    // fast specialization of extend_copy for one addition (for push_back)
    CB_VECTOR_FORCEINLINE void extend_copy(const t_entry & from)
    {
        if ( needmakefit(m_size+1) )
        {
            const size_type oldsize = m_size;
            const size_type oldcapacity = capacity();
            t_entry * pOld = makefit1(m_size + 1);
            cb_entry_array::copy_construct(begin() + m_size,from);
            m_size ++;
            makefit2(pOld, oldsize, oldcapacity);
        }
        else
        {
            cb_entry_array::copy_construct(begin() + m_size,from);
            m_size ++;
        }
    }

    // see notes on base extend_copy
    CB_VECTOR_FORCEINLINE void extend_copy(const t_entry & from,const size_type count)
    {
        if ( needmakefit(m_size+count) )
        {
            const size_type oldsize = m_size;
            const size_type oldcapacity = capacity();
            t_entry * pOld = makefit1(m_size + count);
            for(size_type i=0;i<count;i++)
            {
                cb_entry_array::copy_construct(begin() + m_size + i,from);
            }
            m_size += count;
            makefit2(pOld, oldsize, oldcapacity);
        }
        else
        {
            for(size_type i=0;i<count;i++)
            {
                cb_entry_array::copy_construct(begin() + m_size + i,from);
            }
            m_size += count;
        }
    }

    // reduce size and destruct stuff left behind :
    CB_VECTOR_FORCEINLINE void shrink(const size_type newsize)
    {
        CB_ASSERT( newsize >= 0 && newsize <= m_size );
        const size_type count = m_size - newsize;
        cb_entry_array::destruct(begin() + newsize,count);
        m_size = newsize;
    }

    //-----------------------------------------

    void reserve(const size_type newcap)
    {
        if ( needmakefit(newcap) )
        {
            const size_type oldcapacity = capacity();
            makefit2( makefit1( newcap ) , m_size, oldcapacity);
        }
    }

    // release frees any memory allocated and resizes to zero
    void release()
    {
        shrink(0);
        if ( m_begin )
            CB_FREE(m_begin,m_capacity*sizeof(t_entry));
        //CBFREE(m_begin);
        init();
    }

    void swap(this_type & other)
    {
        AssignmentSwap(m_begin,other.m_begin);
        AssignmentSwap(m_size,other.m_size);
        AssignmentSwap(m_capacity,other.m_capacity);
    }

    void insert(const size_type n_pos,
                 const t_entry * first, const t_entry * last);

    //-----------------------------------------

    // assign_construct :
    //  used by iterator-range and copy constructor for whole vectors
    //  fills out a randbool-new (empty) cb_vector_base with the data from a range of iterators
    template <class input_iterator>
    void assign_construct(const input_iterator first,const input_iterator last)
    {
        CB_ASSERT( m_size == 0 );
        // w64
        const size_type count = (size_type)( last - first );
        if ( needmakefit(count) )
        {
            const size_type oldcapacity = capacity();
            t_entry * pOld = makefit1(count);
            m_size = count;
            cb_entry_array::uninitialized_copy(first, last, begin() );
            makefit2(pOld,(size_type)0,oldcapacity);
        }
        else
        {
            m_size = count;
            cb_entry_array::uninitialized_copy(first, last, begin() );
        }
    }

    // specialization to simple iterators :
    void assign_construct(const t_entry * const first,const t_entry * const last)
    {
        CB_ASSERT( m_size == 0 );
        // w64
        const size_type count = (size_type)( last - first );
        if ( needmakefit(count) )
        {
            const size_type oldcapacity = capacity();
            t_entry * pOld = makefit1(count);
            m_size = count;
            cb_entry_array::copy_construct(begin(),first,count);
            makefit2(pOld,0,oldcapacity);
        }
        else
        {
            m_size = count;
            cb_entry_array::copy_construct(begin(),first,count);
        }
    }

    //-----------------------------------------
    // simple accessors :

    t_entry *           begin()         { return m_begin; }
    const t_entry *     begin() const   { return m_begin; }
    t_entry *           end()           { return begin()+m_size; }
    const t_entry *     end() const     { return begin()+m_size; }
    size_type           size() const    { return m_size; }
    size_type           capacity() const { return m_capacity; }
    size_type           max_size() const { return (1UL)<<30; }

    //-----------------------------------------
    
    //-------------------------------------------------------
protected :

    template<typename Type> 
    static inline void AssignmentSwap(Type &a,Type &b)
    {
        Type c = a;  // Type c(a);
        a = b;
        b = c;
    }
    
    CB_VECTOR_FORCEINLINE bool needmakefit(const size_type newsize) const
    {
        return (newsize > m_capacity);
    }

    // makefit1
    // returns the *old* pointer for passing into makefit2
    //
    t_entry * makefit1(const size_type newsize)
    {
        const size_type oldsize = m_size;

        CB_ASSERT( needmakefit(newsize) );

        t_entry * pOld = m_begin;

        #ifdef CB_VECTOR_MAX_GROW_BYTES
        // Be much more careful about growing the memory conservatively.  This changes
        //  push_back from amortized O(N) to O(N^2), but results in tighter vectors.

        enum { c_maxGrowBytes = CB_VECTOR_MAX_GROW_BYTES }; // 1 MB
        enum { c_maxGrowCount = c_maxGrowBytes/sizeof(t_entry) };
        CB_COMPILER_ASSERT( c_maxGrowCount > 1 );
        // c_maxGrowCount should limit the doubling, but not the passed in newsize
        
        // m_capacity is 0 the first time we're called
        // newsize can be passed in from reserve() so don't put a +1 on it

        // grow by doubling until we get to max grow count, then grow linearly
        size_type newcapacity = CB_MIN( m_capacity * 2 , (m_capacity + c_maxGrowCount) );
        
        #else
        
        size_type newcapacity = m_capacity * 2;
        
        #endif // CB_VECTOR_MAX_GROW_BYTES
        
        newcapacity = CB_MAX( newcapacity, newsize );

        size_type newbytes;
            
        // if on constant should optimize out
        if ( sizeof(t_entry) == 1 )
        {
            // round up newcapacity to be a multiple of 8
            newcapacity = (newcapacity+7)&(~7);
            newbytes = newcapacity;
        }
        else
        {
            newbytes = newcapacity * sizeof(t_entry);
            
            if ( newbytes > 65536 )
            {
                // align newbytes up :
                newbytes = (newbytes + 65535) & (~65535);
            }
            else
            {
                if ( newbytes < 512 )
                {
                    newbytes = (newbytes + 15) & (~15);
                }
                else
                {
                    // align up to 4096 :
                    newbytes = (newbytes + 4095) & (~4095);
                }
            }
            
            // align newcapacity down :
            newcapacity = newbytes / sizeof(t_entry);
        }

        CB_ASSERT( newcapacity >= newsize );            
        CB_ASSERT( newbytes >= newcapacity * sizeof(t_entry) );
        
        t_entry * pNew = (t_entry *) CB_ALLOC( newbytes );

        CB_ASSERT_MALLOC( pNew != 0 );

        // copy existing :
        #ifdef CB_VECTOR_USE_SWAP
        // swap instead of copy can be a big performance advantage
        //  this requires entries to have a default constructor !!
        cb_entry_array::swap_construct(pNew,pOld,oldsize);
        #else
        cb_entry_array::copy_construct(pNew,pOld,oldsize);
        #endif
        
        m_begin = pNew;
        m_capacity = newcapacity;
        // m_size not changed

        return pOld;
    }

    void makefit2(t_entry * pOld, const size_type oldsize, const size_type oldcapacity)
    {
        if ( pOld )
        {
            cb_entry_array::destruct(pOld,oldsize);

            CB_FREE(pOld,oldcapacity*sizeof(t_entry));
        }
    }

private :
    //-----------------------------------------
    // data :

    t_entry *   m_begin;
    size_type   m_size;     // how many in use
    size_type   m_capacity; // how many allocated

    void init()
    {
        m_begin = 0;
        m_capacity = 0;
        m_size = 0;
    }

    //-----------------------------------------
}; // end cb_vector_base

/**
 cb_vector_base::insert for a chunk of elements
 this does NOT work if the elements to insert are from the same vector,
 since insert can cause allocation, and the elements to add are referred-to by pointer
**/
template<typename t_entry> void cb_vector_base<t_entry>::insert(
        const size_type n_pos,
        const t_entry * first, 
        const t_entry * last)
{
    const size_type n_insert = (size_type)( last - first );
    const size_type n_insert_end = (size_type) ( n_pos + n_insert );
    const size_type old_size = size();
    const size_type oldcapacity = capacity();

    t_entry * pOld;
    if ( needmakefit( m_size+n_insert ) )
        pOld = makefit1( m_size + n_insert);
    else
        pOld = 0;

    m_size += n_insert;

    if ( n_insert_end > old_size )
    {
        // copy construct out to n_insert_end
        //  from the insert chunk
        const ptrdiff_t n_insert_copy = old_size - n_pos;
        const ptrdiff_t n_insert_append = n_insert_end - old_size;
        CB_ASSERT( n_insert_copy >= 0 && n_insert_append >= 0);
        CB_ASSERT( (n_insert_copy + n_insert_append) == n_insert );

        cb_entry_array::copy_construct( begin() + old_size, first + n_insert_copy, n_insert_append );

        // now copy from old end to new end
        cb_entry_array::copy_construct( begin() + old_size + n_insert_append, begin() + n_pos, n_insert_copy );

        // finally copy in the n_insert_copy chunk :
        cb_entry_array::copy( begin() + n_pos, first, n_insert_copy );
    }
    else
    {
        cb_entry_array::copy_construct( begin() + old_size, begin() + old_size - n_insert, n_insert );
    
        t_entry * newpos = begin() + n_pos;
    
        const ptrdiff_t count = old_size - n_insert_end;

        cb_entry_array::move( newpos + n_insert, newpos, count );
                
        cb_entry_array::copy( newpos, first, n_insert );
    }
    
    if ( pOld )
        makefit2(pOld, old_size, oldcapacity);
}

//}{=======================================================================================
// vector

template <typename t_entry> class CB_VECTOR : 
    protected cb_vector_base<t_entry>
{
public:
    //----------------------------------------------------------------------
    // typedefs :
    typedef t_entry             value_type;
    typedef t_entry*            iterator;
    typedef const t_entry*      const_iterator;
    typedef t_entry&            reference;
    typedef const t_entry&      const_reference;
    typedef CB_SIZETYPE         size_type;
    
    typedef CB_VECTOR<t_entry>  this_type;
    typedef cb_vector_base<t_entry> parent_type;

    //----------------------------------------------------------------------
    // constructors

    CB_VECTOR_FORCEINLINE  CB_VECTOR() { }
    /*CB_VECTOR_FORCEINLINE*/ ~CB_VECTOR() { }

    CB_VECTOR_FORCEINLINE CB_VECTOR(const size_type size,const value_type & init)
    {
        //parent_type::extend_default(size); 
        parent_type::extend_copy(init,size); 
    }

    CB_VECTOR_FORCEINLINE CB_VECTOR(const this_type & other) :
        parent_type(other)
    {
    }

    template <class input_iterator>
    CB_VECTOR_FORCEINLINE CB_VECTOR(const input_iterator first,const input_iterator last)
        : parent_type(first,last)
    {
    }
    
    //---------------------------------------------------------------------------
    // simple accessors :

    // iterator support
    iterator        begin()         { return parent_type::begin(); }
    const_iterator  begin() const   { return parent_type::begin(); }
    iterator        end()           { return parent_type::end(); }
    const_iterator  end() const     { return parent_type::end(); }

    // at() with range check
    reference at(const size_type i)
    {
        CB_ASSERT( i >= 0 && i < parent_type::size() );
        return *(parent_type::begin() + i); 
    }
    const_reference at(const size_type i) const
    {
        CB_ASSERT( i >= 0 && i < parent_type::size() );
        return *(parent_type::begin() + i); 
    }

    // operator[]
    reference       operator[](const size_type i)       { return at(i); }
    const_reference operator[](const size_type i) const { return at(i); }

    // front() and back()
    reference       front()         { return at(0); }
    const_reference front() const   { return at(0); }
    reference       back()          { return at(parent_type::size()-1); }
    const_reference back() const    { return at(parent_type::size()-1); }

    // size queries :
    size_type   size() const        { return parent_type::size(); }
    bool        empty() const       { return parent_type::size() == 0; }
    size_type   capacity() const    { return parent_type::capacity(); }
    size_type   max_size() const    { return parent_type::max_size(); }

    int         size32() const      { return (int)(size()); }
    
    void reserve(const size_type newcap)    { parent_type::reserve(newcap); }

    //---------------------------------------------------------------------------
    // mutators :

    void push_back(const value_type & e)
                                    { parent_type::extend_copy(e); }

    void push_back()                { parent_type::extend_default(1); }

    // push_back_no_construct is dangerous!!  You must immediately construct it yourself!!
    void push_back_no_construct()   { parent_type::extend_no_construct(1); }
    
    void pop_back()                 { parent_type::shrink( parent_type::size() - 1 ); }

    void clear()                    { parent_type::shrink(0); }

    void resize(const size_type new_size);
    void resize(const size_type new_size, const value_type & e);

    //----------------------------------------------------------------------
    // serious entry mutators :

    iterator insert(iterator position, const value_type & e);

    iterator insert(iterator position,
              const_iterator first, const_iterator last);

    iterator erase(iterator position);

    iterator erase(iterator first, iterator last);

    //----------------------------------------------------------------------
    // serious whole mutators :

    void swap(this_type & other)
    {
        parent_type::swap(other);
    }

    //@@ I'm not fond of operator= on a whole vector; it's error prone and expensive, but here for compatibility
    void operator=(const this_type & other)
    {
        assign(other.begin(),other.end());
    }

    //----------------------------------------------------------------------
    // assign :
    //  assign does work fine for iterator ranges that are in yourself

    template <class input_iterator>
    void assign(const input_iterator first,const input_iterator last)
    {
        const size_type count = (size_type)( last - first );
        if ( size() >= count )
        {
            // don't move on top of self; can happen eg. if you do operator= on yourself
            if ( begin() != first )
            {
                cb_entry_array::move( begin(), first, count );
            }
            parent_type::shrink(count);
        }
        else
        {
            CB_ASSERT( ! is_iterator_in_range(first) );
            CB_ASSERT( (first == last) || ! is_iterator_in_range(last-1) );
            parent_type::shrink(0);
            parent_type::assign_construct(first,last);
        }
    }

    //----------------------------------------------------------------------
    // extensions :

    // @@ ? what should a call to data() do on an empty vector?  This will assert;
    //  maybe it should return NULL ?
    const t_entry * data() const    { CB_ASSERT( ! empty() ); return &(at(0)); }
    t_entry * data()                { CB_ASSERT( ! empty() ); return &(at(0)); }

    size_type size_bytes() const { return size() * sizeof(t_entry); } 
    void memset_zero() { memset(data(),0,size_bytes()); }

    // tighten releases memory that's not in use
    void tighten()
    {
        if ( capacity() != size() )
        {
            this_type other(*this);
            swap(other);
        }
    }
    
    // release frees any memory allocated and resizes to zero
    void release()
    {
        parent_type::release();
    }

    // fast unordered erase :
    //  now with STL-compliant signature
    iterator erase_u(iterator position)
    {
        CB_ASSERT( is_iterator_in_range(position) );
        if ( position != end()-1 )
            *position = back();
        pop_back();
        return position;
    }

    // erase_u an index :
    void erase_u(const size_type i)
    {
        if ( i != size()-1 )
            at(i) = back();
        pop_back();
    }

    // member function find :
    // just write it outrselves so we don't have to bring in <algorithm>

    template <class what_to_find>
    iterator find(const what_to_find & what) 
    {
        for(iterator it = begin();it < end();it++)
        {
            if ( *it == what )
                return it;
        }
        return end();
    }

    template <class what_to_find>
    const_iterator find(const what_to_find & what) const 
    {
        for(const_iterator it = begin();it < end();it++)
        {
            if ( *it == what )
                return it;
        }
        return end();
    }

    template <class _Predicate>
    iterator find_if(_Predicate __pred)
    {
        for(iterator it = begin();it < end();it++)
        {
            if ( __pred(*it) )
                return it;
        }
        return end();
    }

    template <class _Predicate>
    const_iterator find_if(_Predicate __pred) const
    {
        for(iterator it = begin();it < end();it++)
        {
            if ( __pred(*it) )
                return it;
        }
        return end();
    }

    // some sugar :

    template <class other_vector>
    void appendv( const other_vector & other )
    {
        insert( end(), other.begin(), other.end() );
    }

    template <class other_vector>
    void insertv(iterator position, const other_vector & other )
    {
        insert( position, other.begin(), other.end() );
    }

    void append(const_iterator first, const_iterator last)
    {
        insert( end(), first, last );
    }
    
    void append(const_iterator first, const size_type count)
    {
        insert( end(), first, first + count );
    }           

    template <class other_vector>
    void assignv(const other_vector & other )
    {
        assign( other.begin(), other.end() );
    }
    

    //----------------------------------------------------------------------

private:
    //----------------------------------------------------------------------
    template <class input_iterator>
    bool is_iterator_in_range(const input_iterator it) const
    {
        return ( it >= begin() && it < end() );
    }
    template <class input_iterator>
    bool is_iterator_in_range_or_end(const input_iterator it) const
    {
        return ( it >= begin() && it <= end() );
    }   

}; // end vector

//}{=======================================================================================
// vector serious mutators

#define T_PRE1 template<typename t_entry>
#define T_PRE2 CB_VECTOR<t_entry>
#define T_PRE_VOID  T_PRE1 void T_PRE2
#define T_PRE_IT    T_PRE1 typename CB_VECTOR<t_entry>::iterator T_PRE2

//----------------------------------------------------------------------
// serious entry mutators :

T_PRE_IT::insert(const iterator position, const value_type & e)
{
    // not maximally efficient if position == end; use push_back
    CB_ASSERT( is_iterator_in_range_or_end(position) );

    const ptrdiff_t n = ( position - begin() );
    CB_ASSERT( n <= (ptrdiff_t) size() );

    const ptrdiff_t move_count = size() - n;

    if ( move_count == 0 )
    {
        push_back(e);
        return begin();
    }
    else
    {
        // extend by copy construction :
        if ( size() > 0 )
        {
            parent_type::extend_copy( back() );
        }
        // now cannot use "position" any more !

        iterator newpos = begin() + n;
        cb_entry_array::move(newpos+1,newpos, move_count );

        *newpos = e;

        return newpos;
    }
}

T_PRE_IT::insert(iterator position,
          const_iterator first, const_iterator last)
{
    CB_ASSERT( is_iterator_in_range_or_end(position) );
    CB_ASSERT( ! is_iterator_in_range(first) );
    CB_ASSERT( (first == last) || ! is_iterator_in_range(last-1) );

    const ptrdiff_t n_pos = ( position - begin() );

    parent_type::insert( (size_type) n_pos,first,last);

    return begin() + n_pos;
}

T_PRE_IT::erase(const iterator position)
{
    CB_ASSERT( is_iterator_in_range(position) );
    CB_ASSERT( position != end() );
    const ptrdiff_t n_pos = ( position - begin() );
    
    // slide it down :
    const ptrdiff_t newsize = size() - 1;
    cb_entry_array::move(position,position+1, newsize - n_pos );

    parent_type::shrink( (size_type) newsize);
    
    return begin() + n_pos;
}

T_PRE_IT::erase(const iterator first, const iterator last)
{
    CB_ASSERT( is_iterator_in_range_or_end(first) );
    CB_ASSERT( is_iterator_in_range_or_end(last) );
    if ( last <= first )
        return end();

    const ptrdiff_t ifirst = (first - begin());
    const ptrdiff_t ilast  = (last - begin() );
    const ptrdiff_t num_removed = ilast - ifirst;
    const ptrdiff_t num_to_move = parent_type::size() - ilast;

    cb_entry_array::move(first,last, num_to_move);
    parent_type::shrink( (size_type)( parent_type::size() - num_removed ) );
    
    return begin() + ifirst;
}

T_PRE_VOID::resize(const size_type new_size)
{
    // if shrinking, must clear things being left :
    if ( new_size < parent_type::size() )
    {
        parent_type::shrink(new_size);
    }
    else
    {
        parent_type::extend_default(new_size - parent_type::size());
    }
}

T_PRE_VOID::resize(const size_type new_size, const value_type & e)
{
    // if shrinking, must clear things being left :
    if ( new_size < parent_type::size() )
    {
        parent_type::shrink(new_size);
    }
    else
    {
        const size_type count = new_size - parent_type::size();
        parent_type::extend_copy(e,count);
    }
}

#undef T_PRE_IT
#undef T_PRE_VOID
#undef T_PRE1 
#undef T_PRE2

//}{=======================================================================================

#ifdef _MSC_VER
//#pragma pack(pop)
#pragma warning(pop)
#endif

/*
// partial specialize swap_functor to all vectors
//  for cb::Swap

template<class t_entry> 
struct swap_functor< cb::vector<t_entry> >
{
    void operator () ( cb::vector<t_entry> & _Left, cb::vector<t_entry> & _Right)
    {
        _Left.swap(_Right);
    }
};
*/

#ifdef CB_VECTOR_NAMESPACE
}; // namespace
#endif

//}{=======================================================================================


#endif // CB_VECTOR_H_INCLUDED
