// Control.hpp  Utilities for control flow
//

#pragma once

#include "stdafx.h"

#include <memory>
#include <string>
#include <atomic>


namespace Ctrl
{
    // Systematic ErrorHandling in C++
    // Prepared for The C++ and Beyond Seminar 2012
    // Andrei Alexandrescu, Ph.D.
    //
    template<class Fun> 
    class ScopeGuard
    {
        Fun f_;
        bool active_;
    public: 
        ScopeGuard(Fun f)
            : f_(std::move(f)) 
            , active_(true)
        { }
        
        ~ScopeGuard() { if (active_) f_(); } 
        
        void dismiss() { active_ = false; }
        
        ScopeGuard() = delete;       
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

        ScopeGuard(ScopeGuard&& rhs)
            :f_(std::move(rhs.f_)), active_(rhs.active_) 
        { rhs.dismiss(); }
    };

    template<class Fun> 
    ScopeGuard<Fun> scopeGuard(Fun f)
    {
        return ScopeGuard<Fun>(std::move(f));
    }


    /**** use RAII pattern for Try..Finally.  C++ spec section 6.6, 6.6.3. ****/
    /*
    This seems a lot of work to get a Try..Finally pattern and I wonder how efficient the code is.
    But Stroustrup says it is the best!  And C++ has no "finally", so, let's experiment and see.
    */
    template<typename Guard>
    struct ExcludedRegion
    {
        static void Exclude(_In_ Guard& lock)
        {
            throw std::runtime_error{ "must be specialized - did you include UtilitiesWin?" };
        }
        static void Release(_In_ Guard& lock)
        {
            throw std::runtime_error{ "must be specialized - did you include UtilitiesWin?" };
        }
    };

    template<typename Guard>
    struct SharedRegion
    {
        static void Share(_In_ Guard& lock)
        {
            throw std::runtime_error{ "must be specialized - did you include UtilitiesWin?" };
        }
        static void Release(_In_ Guard& lock)
        {
            throw std::runtime_error{ "must be specialized - did you include UtilitiesWin?" };
        }
    };

    // Exclusive region. Acquire a exclusive lock when contruct,
    // releasing it when destructed. 
    //
    template<typename Guard>
    class Exclude
    {
        Guard* m_guard;
    public:
        Exclude(_In_ Guard& guard)
        {
            m_guard = nullptr;
            ExcludedRegion<Guard>::Exclude(guard);
            m_guard = &guard;
        }

        ~Exclude()
        {
            if (nullptr != m_guard)
                ExcludedRegion<Guard>::Release(*m_guard);
            m_guard = nullptr;
        }
    };

    template<typename Guard>
    class Share
    {
        Guard* m_guard;
    public:
        Share(
            // an object supporting shared reader lock pattern
            _In_ Guard& guard
            )
        {
            m_guard = nullptr;
            SharedRegion<Guard>::Share(guard);
            m_guard = &guard;
        }

        ~Share()
        {
            if (nullptr != m_guard)
                SharedRegion<Guard>::Release(*m_guard);
            m_guard = nullptr;
        }
    };

    /*********** end of the Try..Finally region *********************/

}
