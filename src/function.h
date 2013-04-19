/*
 * Copyright (c) 2013 Björn Aili
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 * distribution.
 */

/*
 * Big thanks to Malte Skarupke for the original implementation this
 * version is based on! See
 * http://probablydance.com/2013/01/13/a-faster-implementation-of-stdfunction/
 * for details.
 */
#ifndef FTL_FUNCTION_H
#define FTL_FUNCTION_H

#include <functional>
#include <memory>
#include "type_functions.h"

namespace ftl {

	template<typename,typename...> class function;

	template<typename>
	struct force_function_heap_allocation : std::false_type {};

	// Unnamed namespace for internal details
	namespace {
		struct manager_storage_type;

		struct functor_padding {
		protected:
			size_t padding_fst;
			size_t padding_snd;
		};

		struct empty_struct {};

		template<typename R, typename...Ps>
		R empty_call(const functor_padding&, Ps...) {
			throw std::bad_function_call();
		}

		template<typename T, typename Alloc>
		struct is_inplace_allocated {
			static const bool value
				// so that it fits
				= sizeof(T) <= sizeof(functor_padding)

				// so that it will be aligned
				&& std::alignment_of<functor_padding>::value % std::alignment_of<T>::value == 0

				// so that we can offer noexcept move
				&& std::is_nothrow_move_constructible<T>::value

				// so that the user can override it
				&& !force_function_heap_allocation<T>::value;
		};

		template<typename T>
		T to_functor(T&& func) {
			return std::forward<T>(func);
		}
		template<typename R, typename C, typename...Ps>
		auto to_functor(R (C::*func)(Ps...)) -> decltype(std::mem_fn(func)) {
			return std::mem_fn(func);
		}
		template<typename R, typename C, typename...Ps>
		auto to_functor(R (C::*func)(Ps...) const) -> decltype(std::mem_fn(func)) {
			return std::mem_fn(func);
		}

		template<typename T>
		struct functor_type {
			using type = decltype(to_functor(std::declval<T>()));
		};

		template<typename T>
		bool is_null(const T&) {
			return false;
		}

		template<typename R, typename...Ps>
		bool is_null(R (* const & fp)(Ps...)) {
			return fp == nullptr;
		}

		template<typename R, typename C, typename...Ps>
		bool is_null(R (C::* const & fp)(Ps...)) {
			return fp == nullptr;
		}

		template<typename R, typename C, typename...Ps>
		bool is_null(R (C::* const & fp)(Ps...) const) {
			return fp == nullptr;
		}

		template<typename, typename>
		struct is_valid_function_argument {
			static const bool value = false;
		};

		template<typename R, typename...Ps>
		struct is_valid_function_argument<function<R, Ps...>, R (type_seq<Ps...>)> {
			static const bool value = false;
		};

		template<typename T, typename R, typename...Ps>
		struct is_valid_function_argument<T, R (type_seq<Ps...>)> {

			template<typename U>
			static decltype(to_functor(std::declval<U>())(std::declval<Ps>()...)) check(U *);

			template<typename>
			static empty_struct check(...);

			static const bool value = std::is_convertible<decltype(check<T>(nullptr)), R>::value;
		};

		enum function_manager_calls
		{
			call_move_and_destroy,
			call_copy,
			call_copy_functor_only,
			call_destroy,
		};

		template<typename T, typename Allocator>
		void* function_manager(
				void* first_arg,
				void* second_arg,
				function_manager_calls call_type);

		typedef void *(*manager_type)(void*, void*, function_manager_calls);

		struct manager_storage_type {

			template<typename Allocator>
			Allocator& get_allocator() noexcept {
				return reinterpret_cast<Allocator&>(manager);
			}

			template<typename Allocator>
			const Allocator& get_allocator() const noexcept {
				return reinterpret_cast<const Allocator&>(manager);
			}

			functor_padding functor;
			manager_type manager;
		};

		template<typename T, typename Allocator, typename Enable = void>
		struct function_manager_inplace_specialisation {

			template<typename R,typename...Ps>
			static R call(const functor_padding& storage, Ps... ps) {
				// do not call get_functor_ref because I want this function to
				// be fast in debug when nothing gets inlined
				return
					reinterpret_cast<T&>(
						const_cast<functor_padding&>(storage))(std::forward<Ps>(ps)...);
			}

			static void store_functor(manager_storage_type& storage, T to_store) {
				new (&get_functor_ref(storage)) T(std::forward<T>(to_store));
			}

			static void move_functor(manager_storage_type& lhs, manager_storage_type&& rhs) noexcept {
				new (&get_functor_ref(lhs)) T(std::move(get_functor_ref(rhs)));
			}

			static void destroy_functor(Allocator &, manager_storage_type & storage) noexcept {
				get_functor_ref(storage).~T();
			}

			static T& get_functor_ref(const manager_storage_type& storage) noexcept {
				return reinterpret_cast<T&>(const_cast<functor_padding&>(storage.functor));
			}
		};

		template<typename T, typename Allocator>
		struct function_manager_inplace_specialisation<
					T,
					Allocator,
					typename std::enable_if<
						!is_inplace_allocated<T, Allocator>::value>::type> {

			using alloc_traits = std::allocator_traits<Allocator>;
			using ptr_t = typename alloc_traits::pointer;

			template<typename R, typename...Ps>
			static R call(const functor_padding& storage, Ps... ps) {
				// do not call get_functor_ptr_ref because I want this function
				// to be fast in debug when nothing gets inlined
				return
					(*reinterpret_cast<ptr_t&>
						(const_cast<functor_padding&>(storage)))(std::forward<Ps>(ps)...);
			}

			static void store_functor(manager_storage_type& self, T to_store) {

				Allocator & allocator = self.get_allocator<Allocator>();;
				static_assert(
						sizeof(ptr_t) <= sizeof(self.functor),
						"The allocator's pointer type is too big");

				ptr_t* ptr = new (&get_functor_ptr_ref(self)) ptr_t(alloc_traits::allocate(allocator, 1));

				alloc_traits::construct(allocator, *ptr, std::forward<T>(to_store));
			}

			static void move_functor(manager_storage_type& lhs, manager_storage_type&& rhs) noexcept {

				static_assert(
						std::is_nothrow_move_constructible<ptr_t>::value,
						"we can't offer a noexcept swap if the pointer type is not nothrow move constructible");

				new (&get_functor_ptr_ref(lhs)) ptr_t(std::move(get_functor_ptr_ref(rhs)));

				// this next assignment makes the destroy function easier
				get_functor_ptr_ref(rhs) = nullptr;
			}

			static void destroy_functor(Allocator& allocator, manager_storage_type& storage) noexcept {

				ptr_t& pointer = get_functor_ptr_ref(storage);
				if (!pointer)
					return;

				alloc_traits::destroy(allocator, pointer);
				alloc_traits::deallocate(allocator, pointer, 1);
			}

			static T& get_functor_ref(const manager_storage_type& storage) noexcept {
				return *get_functor_ptr_ref(storage);
			}

			static ptr_t& get_functor_ptr_ref(const manager_storage_type& storage) noexcept {
				return reinterpret_cast<ptr_t&>(const_cast<functor_padding&>(storage.functor));
			}
		};

		template<typename T, typename Allocator>
		static void create_manager(manager_storage_type& storage, Allocator&& allocator) {

			new (&storage.get_allocator<Allocator>()) Allocator(std::move(allocator));
			storage.manager = &function_manager<T, Allocator>;
		}

		// this function acts as a vtable. it is an optimization to prevent
		// code-bloat from rtti. see the documentation of boost::function
		template<typename T, typename Allocator>
		void* function_manager(void* first_arg, void* second_arg, function_manager_calls call_type) {

			using specialisation = function_manager_inplace_specialisation<T, Allocator>;

			static_assert(
					std::is_empty<Allocator>::value,
					"the allocator must be an empty class because I don't have space for it");

			switch(call_type) {
			case call_move_and_destroy: {
				manager_storage_type& lhs = *static_cast<manager_storage_type*>(first_arg);
				manager_storage_type& rhs = *static_cast<manager_storage_type*>(second_arg);

				specialisation::move_functor(lhs, std::move(rhs));
				specialisation::destroy_functor(rhs.get_allocator<Allocator>(), rhs);

				create_manager<T, Allocator>(lhs, std::move(rhs.get_allocator<Allocator>()));
				rhs.get_allocator<Allocator>().~Allocator();

				return nullptr;
			}
			case call_copy: {
				manager_storage_type& lhs = *static_cast<manager_storage_type*>(first_arg);

				const manager_storage_type & rhs = *static_cast<const manager_storage_type*>(second_arg);

				create_manager<T, Allocator>(lhs, Allocator(rhs.get_allocator<Allocator>()));

				specialisation::store_functor(lhs, const_cast<const T&>(specialisation::get_functor_ref(rhs)));

				return nullptr;
			}
			case call_destroy: {
				manager_storage_type& self = *static_cast<manager_storage_type *>(first_arg);

				specialisation::destroy_functor(self.get_allocator<Allocator>(), self);

				self.get_allocator<Allocator>().~Allocator();

				return nullptr;
			}
			case call_copy_functor_only:
				specialisation::store_functor(
						*static_cast<manager_storage_type *>(first_arg),
						const_cast<const T&>(
							specialisation::get_functor_ref(
								*static_cast<const manager_storage_type*>(second_arg))));

				return nullptr;

			default:
				return nullptr;
			}
		}

		// Hidden implementation that ftl::function inherits from.
		// Useful because it allows us to define operator() correctly despite
		// the parameter pack in the deriving function being "oddly shaped".
		template<typename...>
		class fn {
		};
	
		template<typename R, typename...Ps>
		class fn<R, type_seq<Ps...>> {
		public:
			using result_type = R;
			using parameter_types = type_seq<Ps...>;

			fn() noexcept {
				initialise_empty();
			}

			fn(std::nullptr_t) noexcept {
				initialise_empty();
			}

			fn(const fn& f) : call(f.call) {
				f.manager_storage.manager(
					&manager_storage,
					const_cast<manager_storage_type*>(&f.manager_storage),
					call_copy);
			}

			fn(fn&& f) noexcept {
				initialise_empty();
				swap(f);
			}

			template<typename F>
			fn(
					F f,
					typename std::enable_if<
						is_valid_function_argument<F, result_type (parameter_types)>::value,
						empty_struct>::type = empty_struct())
			noexcept(is_inplace_allocated<F, std::allocator<typename functor_type<F>::type>>::value) {

				if(is_null(f))
					initialise_empty();

				else {
					using functor_type = typename functor_type<F>::type;
					initialise(to_functor(std::forward<F>(f)), std::allocator<functor_type>());
				}
			}

			template<typename Allocator>
			fn(std::allocator_arg_t, const Allocator&) {
				// ignore the allocator because I don't allocate
				initialise_empty();
			}

			template<typename Allocator>
			fn(std::allocator_arg_t, const Allocator&, std::nullptr_t) {
				// ignore the allocator because I don't allocate
				initialise_empty();
			}

			template<typename Allocator, typename F>
			fn(
					std::allocator_arg_t,
					const Allocator& allocator,
					F functor,
					typename std::enable_if<
						is_valid_function_argument<F, result_type (parameter_types)>::value,
						empty_struct>::type = empty_struct())
			noexcept(is_inplace_allocated<F, Allocator>::value) {

				if(is_null(functor))
					initialise_empty();

				else {
					initialise(to_functor(std::forward<F>(functor)), Allocator(allocator));
				}
			}

			template<typename Allocator>
			fn(
					std::allocator_arg_t,
					const Allocator& allocator,
					const fn& other)
			: call(other.call) {

				using alloc_traits = std::allocator_traits<Allocator>;
				using MyAllocator = typename alloc_traits::template rebind_alloc<function>::other;

				// first try to see if the allocator matches the target type
				manager_type manager_for_allocator
					= &function_manager<
						typename alloc_traits::value_type, Allocator>;

				if(other.manager_storage.manager == manager_for_allocator) {
					create_manager<typename alloc_traits::value_type, Allocator>(
					manager_storage,
					Allocator(allocator));

					manager_for_allocator(
						&manager_storage,
						const_cast<manager_storage_type*>(&other.manager_storage),
						call_copy_functor_only);
				}

				// if it does not, try to see if the target contains my type. this
				// breaks the recursion of the last case. otherwise repeated copies
				// would allocate more and more memory
				else if(other.manager_storage.manager == &function_manager<function, MyAllocator>) {
					create_manager<function, MyAllocator>(manager_storage, MyAllocator(allocator));
					function_manager<function, MyAllocator>(
						&manager_storage,
						const_cast<manager_storage_type*>(&other.manager_storage),
						call_copy_functor_only);
				}

				else
				{
					// else store the other function as my target
					initialise(other, MyAllocator(allocator));
				}
			}

			template<typename Allocator>
			fn(std::allocator_arg_t, const Allocator&, fn&& other) noexcept {
				// ignore the allocator because I don't allocate
				initialise_empty();
				swap(other);
			}

			virtual ~fn() noexcept {
				manager_storage.manager(&manager_storage, nullptr, call_destroy);
			}

			result_type operator()(Ps...ps) const {
				return call(manager_storage.functor, std::forward<Ps>(ps)...);
			}

			operator bool() const noexcept {
				return call != &empty_call<result_type, Ps...>;
			}

			void swap(fn& other) noexcept {
				manager_storage_type temp_storage;

				other.manager_storage.manager(
						&temp_storage,
						&other.manager_storage,
						call_move_and_destroy);

				manager_storage.manager(
						&other.manager_storage,
						&manager_storage,
						call_move_and_destroy);

				temp_storage.manager(
						&manager_storage,
						&temp_storage,
						call_move_and_destroy);

				std::swap(call, other.call);
			}

			fn& operator= (fn other) noexcept {
				swap(other);
				return *this;
			}

			template<typename F>
			fn& operator= (F f) {
				fn other(std::forward<F>(f));
				swap(other);
				return *this;
			}

			template<typename F, typename Allocator>
			void assign(F&& f, const Allocator& alloc)
			noexcept(is_inplace_allocated<F, Allocator>::value) {
				fn(std::allocator_arg, alloc, f).swap(*this);
			}

		protected:
			manager_storage_type manager_storage;
			result_type (*call) (const functor_padding&, Ps...);

			template<typename F, typename Allocator>
			void initialise(F f, Allocator&& alloc) {
				call = &function_manager_inplace_specialisation<F, Allocator>
					::template call<result_type, Ps...>;

				create_manager<F,Allocator>(manager_storage, std::forward<Allocator>(alloc));

				function_manager_inplace_specialisation<F, Allocator>::store_functor(
					manager_storage,
					std::forward<F>(f));
			}

			void initialise_empty() noexcept {
				using fn_type = result_type (*)(Ps...);

				using Allocator = std::allocator<fn_type>;

				static_assert(
					is_inplace_allocated<fn_type, Allocator>::value,
					"The empty function should benefit from small functor optimization");

				create_manager<fn_type, Allocator>(manager_storage, Allocator());
				function_manager_inplace_specialisation<fn_type, Allocator>::store_functor(
					manager_storage, nullptr);

				call = &empty_call<result_type, Ps...>;
			}
		};

	}

	template<typename T, typename...Ts>
	class function : public fn<
	  	typename get_last<T,Ts...>::type,
		typename take_init<T,Ts...>::type> {

	public:

		using function::fn::fn;

		template<typename F>
		function(F f) : function::fn(f) {}

		~function() noexcept = default;

		function& operator= (function f) noexcept {
			function::fn::operator=(f);
			return *this;
		}

		template<typename F>
		auto operator= (F f)
		-> typename std::enable_if<
			is_valid_function_argument<
				F,
				typename function::fn::result_type (typename function::fn::parameter_types)>::value,
			function&>::type
		{
			function::fn::operator= (std::forward<F>(f));
			return *this;
		}
	};

}

#endif
