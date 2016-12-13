/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 2.0
//              Copyright (2014) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact  H. Carter Edwards (hcedwar@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#ifndef KOKKOS_TASKSCHEDULER_HPP
#define KOKKOS_TASKSCHEDULER_HPP

//----------------------------------------------------------------------------

#include <Kokkos_Core_fwd.hpp>

// If compiling with CUDA then must be using CUDA 8 or better
// and use relocateable device code to enable the task policy.
// nvcc relocatable device code option: --relocatable-device-code=true

#if ( defined( KOKKOS_HAVE_CUDA ) )
  #if ( 8000 <= CUDA_VERSION ) && \
      defined( KOKKOS_CUDA_USE_RELOCATABLE_DEVICE_CODE )

  #define KOKKOS_ENABLE_TASKDAG

  #endif
#else
  #define KOKKOS_ENABLE_TASKDAG
#endif

#if defined( KOKKOS_ENABLE_TASKDAG )

//----------------------------------------------------------------------------

#include <Kokkos_MemoryPool.hpp>
#include <impl/Kokkos_Tags.hpp>

//----------------------------------------------------------------------------

namespace Kokkos {

// Forward declarations used in Impl::TaskQueue

template< typename Arg1 = void , typename Arg2 = void >
class Future ;

template< typename Space >
class TaskScheduler ;

template< typename Space >
void wait( TaskScheduler< Space > const & );

template< typename Space >
struct is_scheduler : public std::false_type {};

template< typename Space >
struct is_scheduler< TaskScheduler< Space > > : public std::true_type {};

} // namespace Kokkos

#include <impl/Kokkos_TaskQueue.hpp>

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {
namespace Impl {

/*\brief  Implementation data for task data management, access, and execution.
 *
 *  CRTP Inheritance structure to allow static_cast from the
 *  task root type and a task's FunctorType.
 *
 *    TaskBase< Space , ResultType , FunctorType >
 *      : TaskBase< Space , ResultType , void >
 *      , FunctorType
 *      { ... };
 *
 *    TaskBase< Space , ResultType , void >
 *      : TaskBase< Space , void , void >
 *      { ... };
 */
template< typename Space , typename ResultType , typename FunctorType >
class TaskBase ;

template< typename Space >
class TaskExec ;

} // namespace Impl
} // namespace Kokkos

//----------------------------------------------------------------------------

namespace Kokkos {

/**
 *
 *  Future< space >  // value_type == void
 *  Future< value >  // space == Default
 *  Future< value , space >
 *
 */
template< typename Arg1 , typename Arg2 >
class Future {
private:

  template< typename > friend class TaskScheduler ;
  template< typename , typename > friend class Future ;
  template< typename , typename , typename > friend class Impl::TaskBase ;

  enum { Arg1_is_space  = Kokkos::is_space< Arg1 >::value };
  enum { Arg2_is_space  = Kokkos::is_space< Arg2 >::value };
  enum { Arg1_is_value  = ! Arg1_is_space &&
                          ! std::is_same< Arg1 , void >::value };
  enum { Arg2_is_value  = ! Arg2_is_space &&
                          ! std::is_same< Arg2 , void >::value };

  static_assert( ! ( Arg1_is_space && Arg2_is_space )
               , "Future cannot be given two spaces" );

  static_assert( ! ( Arg1_is_value && Arg2_is_value )
               , "Future cannot be given two value types" );

  using ValueType =
    typename std::conditional< Arg1_is_value , Arg1 ,
    typename std::conditional< Arg2_is_value , Arg2 , void
    >::type >::type ;

  using Space =
    typename std::conditional< Arg1_is_space , Arg1 ,
    typename std::conditional< Arg2_is_space , Arg2 , void
    >::type >::type ;

  using task_base  = Impl::TaskBase< Space , ValueType , void > ;
  using queue_type = Impl::TaskQueue< Space > ;

  task_base * m_task ;

  KOKKOS_INLINE_FUNCTION explicit
  Future( task_base * task ) : m_task(0)
    { if ( task ) queue_type::assign( & m_task , task ); }

  //----------------------------------------

public:

  using execution_space = typename Space::execution_space ;
  using value_type      = ValueType ;

  //----------------------------------------

  KOKKOS_INLINE_FUNCTION
  bool is_null() const { return 0 == m_task ; }

  KOKKOS_INLINE_FUNCTION
  int reference_count() const
    { return 0 != m_task ? m_task->reference_count() : 0 ; }

  //----------------------------------------

  KOKKOS_INLINE_FUNCTION
  void clear()
    { if ( m_task ) queue_type::assign( & m_task , (task_base*)0 ); }

  //----------------------------------------

  KOKKOS_INLINE_FUNCTION
  ~Future() { clear(); }

  //----------------------------------------

  KOKKOS_INLINE_FUNCTION
  constexpr Future() noexcept : m_task(0) {}

  KOKKOS_INLINE_FUNCTION
  Future( Future && rhs )
    : m_task( rhs.m_task ) { rhs.m_task = 0 ; }

  KOKKOS_INLINE_FUNCTION
  Future( const Future & rhs )
    : m_task(0)
    { if ( rhs.m_task ) queue_type::assign( & m_task , rhs.m_task ); }

  KOKKOS_INLINE_FUNCTION
  Future & operator = ( Future && rhs )
    {
      clear();
      m_task = rhs.m_task ;
      rhs.m_task = 0 ;
      return *this ;
    }

  KOKKOS_INLINE_FUNCTION
  Future & operator = ( const Future & rhs )
    {
      if ( m_task || rhs.m_task ) queue_type::assign( & m_task , rhs.m_task );
      return *this ;
    }

  //----------------------------------------

  template< class A1 , class A2 >
  KOKKOS_INLINE_FUNCTION
  Future( Future<A1,A2> && rhs )
    : m_task( rhs.m_task )
    {
      static_assert
        ( std::is_same< Space , void >::value ||
          std::is_same< Space , typename Future<A1,A2>::Space >::value
        , "Assigned Futures must have the same space" );

      static_assert
        ( std::is_same< value_type , void >::value ||
          std::is_same< value_type , typename Future<A1,A2>::value_type >::value
        , "Assigned Futures must have the same value_type" );

      rhs.m_task = 0 ;
    }

  template< class A1 , class A2 >
  KOKKOS_INLINE_FUNCTION
  Future( const Future<A1,A2> & rhs )
    : m_task(0)
    {
      static_assert
        ( std::is_same< Space , void >::value ||
          std::is_same< Space , typename Future<A1,A2>::Space >::value
        , "Assigned Futures must have the same space" );

      static_assert
        ( std::is_same< value_type , void >::value ||
          std::is_same< value_type , typename Future<A1,A2>::value_type >::value
        , "Assigned Futures must have the same value_type" );

      if ( rhs.m_task ) queue_type::assign( & m_task , rhs.m_task );
    }

  template< class A1 , class A2 >
  KOKKOS_INLINE_FUNCTION
  Future & operator = ( const Future<A1,A2> & rhs )
    {
      static_assert
        ( std::is_same< Space , void >::value ||
          std::is_same< Space , typename Future<A1,A2>::Space >::value
        , "Assigned Futures must have the same space" );

      static_assert
        ( std::is_same< value_type , void >::value ||
          std::is_same< value_type , typename Future<A1,A2>::value_type >::value
        , "Assigned Futures must have the same value_type" );

      if ( m_task || rhs.m_task ) queue_type::assign( & m_task , rhs.m_task );
      return *this ;
    }

  template< class A1 , class A2 >
  KOKKOS_INLINE_FUNCTION
  Future & operator = ( Future<A1,A2> && rhs )
    {
      static_assert
        ( std::is_same< Space , void >::value ||
          std::is_same< Space , typename Future<A1,A2>::Space >::value
        , "Assigned Futures must have the same space" );

      static_assert
        ( std::is_same< value_type , void >::value ||
          std::is_same< value_type , typename Future<A1,A2>::value_type >::value
        , "Assigned Futures must have the same value_type" );

      clear();
      m_task = rhs.m_task ;
      rhs.m_task = 0 ;
      return *this ;
    }

  //----------------------------------------

  KOKKOS_INLINE_FUNCTION
  typename task_base::get_return_type
  get() const
    {
      if ( 0 == m_task ) {
        Kokkos::abort( "Kokkos:::Future::get ERROR: is_null()");
      }
      return m_task->get();
    }
};

// Is a Future with the given execution space
template< typename , typename ExecSpace = void >
struct is_future : public std::false_type {};

template< typename Arg1 , typename Arg2 , typename ExecSpace >
struct is_future< Future<Arg1,Arg2> , ExecSpace >
  : public std::integral_constant
      < bool ,
      ( std::is_same< ExecSpace , void >::value ||
        std::is_same< ExecSpace
                    , typename Future<Arg1,Arg2>::execution_space >::value )
      > {};

} // namespace Kokkos

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {

enum class TaskPriority : int { High    = 0
                              , Regular = 1
                              , Low     = 2 };

} // namespace Kokkos

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {
namespace Impl {

//----------------------------------------------------------------------------

template< int TaskEnum , typename DepFutureType >
struct TaskPolicyData
{
  using execution_space = typename DepFutureType::execution_space ;
  using scheduler_type  = TaskScheduler< execution_space > ;

  enum : int { m_task_type = TaskEnum };

  scheduler_type const * m_scheduler ;
  DepFutureType  const   m_dependence ;
  int                    m_priority ;

  TaskPolicyData() = delete ;
  TaskPolicyData( TaskPolicyData && ) = default ;
  TaskPolicyData( TaskPolicyData const & ) = default ;
  TaskPolicyData & operator = ( TaskPolicyData && ) = default ;
  TaskPolicyData & operator = ( TaskPolicyData const & ) = default ;

  KOKKOS_INLINE_FUNCTION
  TaskPolicyData( DepFutureType             && arg_future
                , Kokkos::TaskPriority const & arg_priority )
    : m_scheduler( 0 )
    , m_dependence( arg_future )
    , m_priority( static_cast<int>( arg_priority ) )
    {}

  KOKKOS_INLINE_FUNCTION
  TaskPolicyData( scheduler_type       const & arg_scheduler
                , Kokkos::TaskPriority const & arg_priority )
    : m_scheduler( & arg_scheduler )
    , m_dependence()
    , m_priority( static_cast<int>( arg_priority ) )
    {}
};

} // namespace Impl
} // namespace Kokkos

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {

template< typename ExecSpace >
class TaskScheduler
{
private:

  using track_type = Kokkos::Impl::SharedAllocationTracker ;
  using queue_type = Kokkos::Impl::TaskQueue< ExecSpace > ;
  using task_base  = Impl::TaskBase< ExecSpace , void , void > ;

  track_type   m_track ;
  queue_type * m_queue ;

  //----------------------------------------

public:

  using execution_policy = TaskScheduler ;
  using execution_space  = ExecSpace ;
  using memory_space     = typename queue_type::memory_space ;
  using member_type      = Kokkos::Impl::TaskExec< ExecSpace > ;

  KOKKOS_INLINE_FUNCTION
  TaskScheduler() : m_track(), m_queue(0) {}

  KOKKOS_INLINE_FUNCTION
  TaskScheduler( TaskScheduler && rhs ) = default ;

  KOKKOS_INLINE_FUNCTION
  TaskScheduler( TaskScheduler const & rhs ) = default ;

  KOKKOS_INLINE_FUNCTION
  TaskScheduler & operator = ( TaskScheduler && rhs ) = default ;

  KOKKOS_INLINE_FUNCTION
  TaskScheduler & operator = ( TaskScheduler const & rhs ) = default ;

  TaskScheduler( memory_space const & arg_memory_space
               , unsigned const arg_memory_pool_capacity
               , unsigned const arg_memory_pool_log2_superblock = 12 )
    : m_track()
    , m_queue(0)
    {
      typedef Kokkos::Impl::SharedAllocationRecord
        < memory_space , typename queue_type::Destroy >
          record_type ;

      record_type * record =
        record_type::allocate( arg_memory_space
                             , "TaskQueue"
                             , sizeof(queue_type)
                             );

      m_queue = new( record->data() )
        queue_type( arg_memory_space
                  , arg_memory_pool_capacity
                  , arg_memory_pool_log2_superblock );

      record->m_destroy.m_queue = m_queue ;

      m_track.assign_allocated_record_to_uninitialized( record );
    }

  //----------------------------------------
  /**\brief  Allocation size for a spawned task */
  template< typename FunctorType >
  KOKKOS_FUNCTION
  size_t spawn_allocation_size() const
    {
      using task_type  = Impl::TaskBase< execution_space
                                       , typename FunctorType::value_type
                                       , FunctorType > ;

      return m_queue->allocate_block_size( sizeof(task_type) );
    }

  /**\brief  Allocation size for a when_all aggregate */
  KOKKOS_FUNCTION
  size_t when_all_allocation_size( int narg ) const
    {
      using task_base  = Kokkos::Impl::TaskBase< ExecSpace , void , void > ;

      return m_queue->allocate_block_size( sizeof(task_base) + narg * sizeof(task_base*) );
    }

  //----------------------------------------

  template< int TaskEnum , typename DepFutureType , typename FunctorType >
  KOKKOS_FUNCTION static
  Kokkos::Future< typename FunctorType::value_type , execution_space >
  spawn( Impl::TaskPolicyData<TaskEnum,DepFutureType> const & arg_policy
       , typename task_base::function_type                    arg_function
       , FunctorType                                       && arg_functor
       )
    {
      using value_type  = typename FunctorType::value_type ;
      using future_type = Future< value_type , execution_space > ;
      using task_type   = Impl::TaskBase< execution_space
                                        , value_type
                                        , FunctorType > ;

      queue_type * const queue =
        arg_policy.m_scheduler ? arg_policy.m_scheduler->m_queue : (
        arg_policy.m_dependence.m_task
          ? arg_policy.m_dependence.m_task->m_queue
          : (queue_type*) 0 );

      if ( 0 == queue ) {
        Kokkos::abort("Kokkos spawn without Scheduler or Future" );
      }

      //----------------------------------------
      // Give single-thread back-ends an opportunity to clear
      // queue of ready tasks before allocating a new task

      queue->iff_single_thread_recursive_execute();

      //----------------------------------------

      future_type f ;

      // Allocate task from memory pool
      f.m_task =
        reinterpret_cast< task_type * >(queue->allocate(sizeof(task_type)));

      if ( f.m_task ) {

        // Placement new construction
        new ( f.m_task ) task_type( arg_functor );

        // Reference count starts at two
        // +1 for matching decrement when task is complete
        // +1 for future
        f.m_task->m_queue      = queue ;
        f.m_task->m_ref_count  = 2 ;
        f.m_task->m_alloc_size = sizeof(task_type);
        f.m_task->m_apply      = arg_function ;
        f.m_task->m_task_type  = arg_policy.m_task_type ;
        f.m_task->m_priority   = arg_policy.m_priority ;

        f.m_task->add_dependence( arg_policy.m_dependence.m_task );

        queue->schedule( f.m_task );
        // This task may be updated or executed at any moment,
        // even during the call to 'schedule'.
      }

      return f ;
    }

  template< typename FunctorType , typename A1 , typename A2 >
  KOKKOS_FUNCTION static
  void
  respawn( FunctorType         * arg_self
         , Future<A1,A2> const & arg_dependence
         , TaskPriority  const & arg_priority
         )
    {
      // Postcondition: task is in Executing state

      using value_type  = typename FunctorType::value_type ;
      using task_type   = Impl::TaskBase< execution_space
                                        , value_type
                                        , FunctorType > ;

      task_base * const lock = (task_base *) task_base::LockTag ;
      task_type * const task = static_cast< task_type * >( arg_self );

      task->m_priority = static_cast<int>(arg_priority);

      task->add_dependence( arg_dependence.m_task );

      // Postcondition: task is in Executing-Respawn state
    }

  //----------------------------------------
  /**\brief  Return a future that is complete
   *         when all input futures are complete.
   */
  template< typename A1 , typename A2 >
  KOKKOS_FUNCTION static
  Future< execution_space >
  when_all( Future< A1 , A2 > const arg[] , int narg )
    {
      using future_type = Future< execution_space > ;
      using task_base   = Kokkos::Impl::TaskBase< execution_space , void , void > ;

      future_type f ;

      if ( narg ) {

        queue_type * const queue = arg[0].m_task->m_queue ;

        size_t const size  = sizeof(task_base) + narg * sizeof(task_base*);

        f.m_task =
          reinterpret_cast< task_base * >( queue->allocate( size ) );

        if ( f.m_task ) {

          new( f.m_task ) task_base();

          // Reference count starts at two:
          // +1 to match decrement when task completes
          // +1 for the future
          f.m_task->m_queue      = queue ;
          f.m_task->m_ref_count  = 2 ;
          f.m_task->m_alloc_size = size ;
          f.m_task->m_dep_count  = narg ;
          f.m_task->m_task_type  = task_base::Aggregate ;

          task_base ** const dep = f.m_task->aggregate_dependences();

          // Assign dependences to increment their reference count
          // The futures may be destroyed upon returning from this call
          // so increment reference count to track this assignment.

          for ( int i = 0 ; i < narg ; ++i ) {
            task_base * const t = dep[i] = arg[i].m_task ;
            if ( 0 != t ) {
	      Kokkos::atomic_increment( &(t->m_ref_count) );
            }
          }

          queue->schedule( f.m_task );
          // this when_all may be processed at any moment
        }
      }

      return f ;
    }

  //----------------------------------------

  KOKKOS_INLINE_FUNCTION
  int allocation_capacity() const noexcept
    { return m_queue->m_memory.get_mem_size(); }

  KOKKOS_INLINE_FUNCTION
  int allocated_task_count() const noexcept
    { return m_queue->m_count_alloc ; }

  KOKKOS_INLINE_FUNCTION
  int allocated_task_count_max() const noexcept
    { return m_queue->m_max_alloc ; }

  KOKKOS_INLINE_FUNCTION
  long allocated_task_count_accum() const noexcept
    { return m_queue->m_accum_alloc ; }

  //----------------------------------------

  template< typename S >
  friend
  void Kokkos::wait( Kokkos::TaskScheduler< S > const & );

};

} // namespace Kokkos

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {

//----------------------------------------------------------------------------
// Construct a TaskTeam execution policy

template< typename A1 , typename A2 >
Kokkos::Impl::TaskPolicyData
  < Kokkos::Impl::TaskBase<void,void,void>::TaskTeam
  , Kokkos::Future<A1,A2>
  >
KOKKOS_INLINE_FUNCTION
TaskTeam( Future<A1,A2> const & arg_future
        , TaskPriority  const & arg_priority = TaskPriority::Regular
        )
{
  return
    Kokkos::Impl::TaskPolicyData
      < Kokkos::Impl::TaskBase<void,void,void>::TaskTeam
      , Kokkos::Future<A1,A2> >( arg_future , arg_priority );
}

template< typename ExecSpace >
Kokkos::Impl::TaskPolicyData
  < Kokkos::Impl::TaskBase<void,void,void>::TaskTeam
  , Kokkos::Future<ExecSpace>
  >
KOKKOS_INLINE_FUNCTION
TaskTeam( TaskScheduler< ExecSpace > const & arg_scheduler
        , TaskPriority const & arg_priority = TaskPriority::Regular
        )
{
  return
    Kokkos::Impl::TaskPolicyData
      < Kokkos::Impl::TaskBase<void,void,void>::TaskTeam
      , Kokkos::Future<ExecSpace> >( arg_scheduler , arg_priority );
}

// Construct a TaskSingle execution policy

template< typename A1 , typename A2 >
Kokkos::Impl::TaskPolicyData
  < Kokkos::Impl::TaskBase<void,void,void>::TaskSingle
  , Kokkos::Future<A1,A2>
  >
KOKKOS_INLINE_FUNCTION
TaskSingle( Future<A1,A2> const & arg_future
          , TaskPriority  const & arg_priority = TaskPriority::Regular
          )
{
  return
    Kokkos::Impl::TaskPolicyData
      < Kokkos::Impl::TaskBase<void,void,void>::TaskSingle
      , Kokkos::Future<A1,A2> >( arg_future , arg_priority );
}

template< typename ExecSpace >
Kokkos::Impl::TaskPolicyData
  < Kokkos::Impl::TaskBase<void,void,void>::TaskSingle
  , Kokkos::Future<ExecSpace>
  >
KOKKOS_INLINE_FUNCTION
TaskSingle( TaskScheduler< ExecSpace > const & arg_scheduler
          , TaskPriority const & arg_priority = TaskPriority::Regular
          )
{
  return
    Kokkos::Impl::TaskPolicyData
      < Kokkos::Impl::TaskBase<void,void,void>::TaskSingle
      , Kokkos::Future<ExecSpace> >( arg_scheduler , arg_priority );
}

//----------------------------------------------------------------------------

/**\brief  A host control thread spawns a task with options
 *
 *  1) High, Normal, or Low priority
 *  2) With or without dependence
 *  3) Team or Serial
 */
template< int TaskEnum
        , typename DepFutureType
        , typename FunctorType >
Future< typename FunctorType::value_type
      , typename DepFutureType::execution_space >
host_spawn( Impl::TaskPolicyData<TaskEnum,DepFutureType> const & arg_policy
          , FunctorType                                       && arg_functor
          )
{
  using exec_space = typename DepFutureType::execution_space ;
  using scheduler  = TaskScheduler< exec_space > ;

  using task_type = Impl::TaskBase< exec_space
                                  , typename FunctorType::value_type
                                  , FunctorType > ;

  static_assert( TaskEnum == task_type::TaskTeam ||
                 TaskEnum == task_type::TaskSingle
               , "Kokkos host_spawn requires TaskTeam or TaskSingle" );

  // May be spawning a Cuda task, must use the specialization
  // to query on-device function pointer.
  typename task_type::function_type ptr =
    Kokkos::Impl::TaskQueueSpecialization< exec_space >::
      template get_function_pointer< task_type >();

  return scheduler::spawn( arg_policy , ptr , std::move(arg_functor) );
}

/**\brief  A task spawns a task with options
 *
 *  1) High, Normal, or Low priority
 *  2) With or without dependence
 *  3) Team or Serial
 */
template< int TaskEnum
        , typename DepFutureType
        , typename FunctorType >
Future< typename FunctorType::value_type
      , typename DepFutureType::execution_space >
KOKKOS_INLINE_FUNCTION
task_spawn( Impl::TaskPolicyData<TaskEnum,DepFutureType> const & arg_policy
          , FunctorType                                       && arg_functor
          )
{
  using exec_space = typename DepFutureType::execution_space ;
  using scheduler  = TaskScheduler< exec_space > ;

#if defined( KOKKOS_ACTIVE_EXECUTION_MEMORY_SPACE_HOST ) && \
    defined( KOKKOS_HAVE_CUDA )

  static_assert( ! std::is_same< Kokkos::Cuda , exec_space >::value
               , "Error calling Kokkos::task_spawn for Cuda space within Host code" );

#endif

  using task_type = Impl::TaskBase< exec_space
                                  , typename FunctorType::value_type
                                  , FunctorType > ;

  static_assert( TaskEnum == task_type::TaskTeam ||
                 TaskEnum == task_type::TaskSingle
               , "Kokkos host_spawn requires TaskTeam or TaskSingle" );

  typename task_type::function_type ptr = task_type::apply ;

  return scheduler::spawn( arg_policy , ptr , std::move(arg_functor) );
}

/**\brief  A task respawns itself with options
 *
 *  1) With or without dependence
 *  2) High, Normal, or Low priority
 */
template< typename FunctorType , typename A1 , typename A2 >
void
KOKKOS_INLINE_FUNCTION
respawn( FunctorType         * arg_self
       , Future<A1,A2> const & arg_dependence
       , TaskPriority  const & arg_priority = TaskPriority::Regular
       )
{
  TaskScheduler< typename Future<A1,A2>::execution_space >::
    respawn( arg_self , arg_dependence , arg_priority );
}

template< typename FunctorType , typename ExecSpace >
void
KOKKOS_INLINE_FUNCTION
respawn( FunctorType         * arg_self
       , TaskScheduler< ExecSpace > const &
       , TaskPriority const & arg_priority = TaskPriority::Regular
       )
{
  TaskScheduler< ExecSpace >::
    respawn( arg_self , Future<ExecSpace>() , arg_priority );
}

//----------------------------------------------------------------------------

template< typename A1 , typename A2 >
KOKKOS_INLINE_FUNCTION
Future< typename Future< A1 , A2 >::execution_space >
when_all( Future< A1 , A2 > const arg[]
        , int                     narg
        )
{
  return TaskScheduler< typename Future<A1,A2>::execution_space >::
    when_all( arg , narg );
}

//----------------------------------------------------------------------------
// Wait for all runnable tasks to complete

template< typename ExecSpace >
inline
void wait( TaskScheduler< ExecSpace > const & scheduler )
{ scheduler.m_queue->execute(); }

} // namespace Kokkos

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

#endif /* #if defined( KOKKOS_ENABLE_TASKDAG ) */
#endif /* #ifndef KOKKOS_TASKSCHEDULER_HPP */
