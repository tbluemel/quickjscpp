/*
 * quickjs-cpp
 *
 * Copyright (c) 2020 Thomas Bluemel <thomas@reactsoft.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef __QUICKJS__HPP
#define __QUICKJS__HPP

#include <quickjs.h>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <exception>
#include <functional>
#if 0
#include <iostream>
#define QJSCPP_DEBUG(stmt) \
	do { \
		std::cerr << "[quickjs] " __FILE__ << ':' << __LINE__ << ": " << stmt << std::endl; \
	} while(0)
#else
#define QJSCPP_DEBUG(stmt)
#endif

namespace quickjs
{
	class runtime;
	class context;
	class value;
	class args;
	
	class exception:
		public std::exception
	{
	protected:
		std::string str_;
		
		exception() = default;
		
	public:
		template <typename T>
		exception(T what):
			str_(what)
		{
		}
		
		virtual ~exception()
		{
		}
		
		virtual const char* what() const noexcept
		{
			return str_.c_str();
		}
	};
	
	class invalid_context:
		public exception
	{
	public:
		invalid_context():
			exception("invalid context")
		{
		}
	};
	
	class cstring
	{
		friend class context;
		friend class value;
		
		JSContext* ctx_{nullptr};
		const char* cstr_{nullptr};
		
		cstring(JSContext* ctx, JSValueConst val):
			ctx_(ctx),
			cstr_(JS_ToCString(ctx, val))
		{
		}
		
	public:
		cstring(const cstring&) = delete;
		cstring& operator=(const cstring&) = delete;
		
		cstring() = default;
		
		cstring(cstring&& from):
			ctx_(from.ctx_),
			cstr_(from.cstr_)
		{
			from.ctx_ = nullptr;
			from.cstr_ = nullptr;
		}
		
		cstring& operator=(cstring&& from)
		{
			if (&from != this)
			{
				if (ctx_ && cstr_)
					JS_FreeCString(ctx_, cstr_);
				ctx_ = from.ctx_;
				from.ctx_ = nullptr;
				cstr_ = from.cstr_;
				from.cstr_ = nullptr;
			}
			return *this;
		}
		
		~cstring()
		{
			if (ctx_ && cstr_)
				JS_FreeCString(ctx_, cstr_);
		}
		
		operator std::string() const
		{
			return cstr_ ? std::string(cstr_) : std::string();
		}
		
		operator const char*() const
		{
			return cstr_;
		}
		
		std::string str() const
		{
			return cstr_ ? std::string(cstr_) : std::string();
		}
		
		const char* c_str() const
		{
			return cstr_;
		}
		
		operator bool() const
		{
			return cstr_ != nullptr;
		}
	};
	
	namespace detail
	{
		struct jsvalue_list;
		
		template <typename Owned>
		class owner;
		
		class list_entry
		{
			template <typename Owned> friend class owner;
			list_entry *flink_;
			list_entry *blink_;
			
			inline void do_move(list_entry&& from)
			{
				auto next = from.flink_;
				auto prev = from.blink_;
				next->blink_ = this;
				prev->flink_ = this;
				flink_ = next;
				blink_ = prev;
				from.flink_ = &from;
				from.blink_ = &from;
			}
		public:
			list_entry():
				flink_(this),
				blink_(this)
			{
			}
			
			~list_entry()
			{
				assert(!is_linked());
			}
			
			list_entry(const list_entry&) = delete;
			list_entry& operator=(const list_entry&) = delete;
			
			list_entry& operator=(list_entry&& from)
			{
				if (&from != this)
				{
					if (is_linked())
						unlink();
					if (from.is_linked())
						do_move(std::move(from));
				}
				return *this;
			}
			
			list_entry(list_entry&& from)
			{
				if (from.is_linked())
					do_move(std::move(from));
				else
				{
					flink_ = this;
					blink_ = this;
				}
			}
			
			inline bool is_linked() const
			{
				return flink_ != this && blink_ != this;
			}
			
			inline void unlink()
			{
				if (is_linked())
				{
					auto next = flink_;
					auto prev = blink_;
					next->blink_ = prev;
					prev->flink_ = next;
					flink_ = this;
					blink_ = this;
				}
			}
		};
		
		template <typename Owned>
		class owner
		{
			list_entry head_;
			
		public:
			owner() = default;
			
			owner(const owner&) = delete;
			owner(owner&&) = delete;
			owner& operator=(const owner&) = delete;
			owner& operator=(owner&&) = delete;
			
			~owner()
			{
				assert(!head_.is_linked());
			}
			
			template <typename F>
			void for_each(F f)
			{
				for (auto current = head_.flink_; current != &head_; )
				{
					auto next = current->flink_;
					f(static_cast<Owned*>(current));
					current = next;
				}
			}
			
			inline void insert_head(list_entry& entry)
			{
				assert(&entry != &head_);
				assert(!entry.is_linked());
				list_entry* next = head_.flink_;
				head_.flink_ = &entry;
				entry.blink_ = &head_;
				entry.flink_ = next;
				next->blink_ = &entry;
				if (head_.blink_ == &head_)
					head_.blink_ = &entry;
			}
		};
	}
	
	class value_error:
		public exception
	{
		friend class value;
		std::string stack_;
		
		template <typename T>
		value_error(T val):
			exception(val)
		{
		}
		
		value_error(const cstring& val, const cstring& stack):
			exception(val.str()),
			stack_(stack.str())
		{
		}
	
	public:
		const char* stack() const
		{
			return stack_.c_str();
		}
	};
	
	class value_exception;
	
	template <typename ClassType> class class_def;
	template <typename ClassType> class class_def_shared;
	
	namespace detail
	{
		template <typename T>
		struct func_traits:
			public func_traits<decltype(&T::operator())>
		{
		};
		
		template <typename R, typename... Args>
		struct func_traits<R(*)(Args...)>
		{
			enum { arity = sizeof...(Args) };
			
			typedef R result_type;
			
			template <size_t i>
			struct arg
			{
				typedef typename std::tuple_element<i, std::tuple<Args...>>::type type;
			};
		};
		
		template <typename C, typename R, typename... Args>
		struct func_traits<R(C::*)(Args...) const>
		{
			enum { arity = sizeof...(Args) };
			
			typedef R result_type;
			
			template <size_t i>
			struct arg
			{
				typedef typename std::tuple_element<i, std::tuple<Args...>>::type type;
			};
		};
		
		template <size_t... Ns>
		struct indices
		{
			typedef indices<Ns..., sizeof...(Ns)> next;
		};
		
		template <size_t N>
		struct make_indices
		{
			typedef typename make_indices<N - 1>::type::next type;
		};
		
		template<>
		struct make_indices<0>
		{
			typedef indices<> type;
		};
		
		template <typename... Args>
		struct args_traits
		{
			enum { arity = sizeof...(Args) };
			
			template <size_t i>
			struct arg
			{
				typedef typename std::tuple_element<i, std::tuple<Args...>>::type type;
			};
		};
		
		struct closures_common
		{
			template <typename Func, size_t N>
			static JSValue handle_closure_expand(Func f, JSContext* ctx, JSValueConst this_val, int argc, JSValueConst *argv);
			template <typename Func, size_t N>
			static JSValue handle_closure_expand_with_args(Func f, JSContext* ctx, JSValueConst this_val, int argc, JSValueConst *argv);
		};
		
		template <typename FirstArg>
		struct closures
		{
			template <size_t N, typename R, typename... A>
			struct function
			{
				static JSValue handler(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int /*magic*/, void *opaque)
				{
					typedef R(*Func)(A...);
					Func f = reinterpret_cast<Func>(opaque);
					return closures_common::handle_closure_expand<Func, N>(*f, ctx, this_val, argc, argv);
				}
			};
			
			template<typename R, typename... A>
			static JSValue create(JSContext* ctx, R(*f)(A...))
			{
				using traits = detail::func_traits<decltype(f)>;
				return JS_NewCClosure(ctx, function<traits::arity, R, A...>::handler, traits::arity, 0, reinterpret_cast<void*>(f), nullptr);
			}
			
			template <typename Func, size_t N>
			struct functor
			{
				static JSValue handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst *argv, int /*magic*/, void* opaque)
				{
					Func* f = reinterpret_cast<Func*>(opaque);
					return closures_common::handle_closure_expand<Func, N>(*f, ctx, this_val, argc, argv);
				}
			};
			
			template<typename Func>
			static JSValue create(JSContext* ctx, Func f)
			{
				using traits = detail::func_traits<decltype(f)>;
				std::unique_ptr<decltype(f)> fcopy(new Func(std::move(f)));
				JSValue ret = JS_NewCClosure(ctx, functor<decltype(f), traits::arity>::handler, traits::arity, 0, reinterpret_cast<void*>(fcopy.get()),
					[](void* opaque)
					{
						delete reinterpret_cast<Func*>(opaque);
					});
				if (JS_IsException(ret))
					return ret;
				fcopy.release();
				return ret;
			}
		};
		
		template<>
		struct closures<const args&>
		{
			template <size_t N, typename R, typename... A>
			struct function
			{
				static JSValue handler(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int /*magic*/, void *opaque)
				{
					typedef R(*Func)(A...);
					Func f = reinterpret_cast<Func>(opaque);
					return closures_common::handle_closure_expand_with_args<Func, N>(*f, ctx, this_val, argc, argv);
				}
			};
			
			template<typename R, typename... A>
			static JSValue create(JSContext* ctx, R(*f)(A...))
			{
				using traits = detail::func_traits<decltype(f)>;
				return JS_NewCClosure(ctx, function<traits::arity - 1, R, A...>::handler, traits::arity - 1, 0, reinterpret_cast<void*>(f), nullptr);
			}
			
			template <typename Func, size_t N>
			struct functor
			{
				static JSValue handler(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv, int /*magic*/, void* opaque)
				{
					Func* f = reinterpret_cast<Func*>(opaque);
					return closures_common::handle_closure_expand_with_args<Func, N>(*f, ctx, this_val, argc, argv);
				}
			};
			
			template<typename Func>
			static JSValue create(JSContext* ctx, Func f)
			{
				using traits = detail::func_traits<decltype(f)>;
				std::unique_ptr<decltype(f)> fcopy(new Func(std::move(f)));
				JSValue ret = JS_NewCClosure(ctx, functor<decltype(f), traits::arity - 1>::handler, traits::arity - 1, 0, reinterpret_cast<void*>(fcopy.get()),
					[](void* opaque)
					{
						delete reinterpret_cast<Func*>(opaque);
					});
				if (JS_IsException(ret))
					return ret;
				fcopy.release();
				return ret;
			}
		};
		
		// based on: https://stackoverflow.com/a/30766365
		template <typename T>
		struct is_iterator
		{
			static char test(...);
			
			template <typename U,
				typename = typename std::iterator_traits<U>::difference_type,
				typename = typename std::iterator_traits<U>::pointer,
				typename = typename std::iterator_traits<U>::reference,
				typename = typename std::iterator_traits<U>::value_type,
				typename = typename std::iterator_traits<U>::iterator_category
			> static long test(U&&);
			
			constexpr static bool value = std::is_same<decltype(test(std::declval<T>())),long>::value;
		};
		
		template<typename Arg1, typename Arg2>
		struct is_iterator_args
		{
			constexpr static bool value = is_iterator<Arg1>::value && is_iterator<Arg2>::value;
		};
		
		struct functions
		{
			static value call_common_args(const value& func, JSContext* ctx, const value& thisObj, size_t acnt, JSValue* avals);
			template <typename... Args>
			static value call_common(const value& func, JSContext* ctx, const value& thisObj, Args&&... a);
			template <typename Begin, typename End>
			static value call_common_it(const value& func, JSContext* ctx, const value& thisObj, Begin&& begin, End&& end);
			
			static value call(const value& func, JSContext* ctx, const value& thisObj);
			template <typename A>
			static value call(const value& func, JSContext* ctx, const value& thisObj, A&& a);
			template <typename A1, typename A2, typename... Args,
				typename = typename std::enable_if<!is_iterator_args<A1, A2>::value>::type>
			static value call(const value& func, JSContext* ctx, const value& thisObj, A1&& a1, A2&& a2, Args&&... a);
			template <typename Begin, typename End,
				typename = typename std::enable_if<is_iterator_args<Begin, End>::value>::type>
			static value call(const value& func, JSContext* ctx, const value& thisObj, Begin&& begin, End&& end);
		};
		
		struct classes
		{
			template <typename ClassType, typename std::enable_if<std::is_base_of<class_def<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
			static ClassType* get_raw_inst(JSValueConst val);
			
			template <typename ClassType, typename std::enable_if<std::is_base_of<class_def_shared<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
			static std::shared_ptr<ClassType>* get_raw_inst(JSValueConst val);
			
			template <typename ClassType, typename std::enable_if<std::is_base_of<class_def<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
			static ClassType* get_inst(JSValueConst val);
			
			template <typename ClassType, typename std::enable_if<std::is_base_of<class_def_shared<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
			static std::shared_ptr<ClassType> get_inst(JSValueConst val);
			
			template <typename ClassType, typename std::enable_if<std::is_base_of<class_def_shared<ClassType>, decltype(ClassType::class_definition)>{}, int>::type = 0>
			static JSValue class_make_object_for_inst(JSContext* ctx, const std::shared_ptr<ClassType>& inst);
			
			template <typename ClassType, typename std::enable_if<std::is_base_of<class_def<ClassType>, decltype(ClassType::class_definition)>{}, int>::type = 0>
			static JSValue class_make_inst(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst *argv);
			
			template <typename ClassType, typename std::enable_if<std::is_base_of<class_def_shared<ClassType>, decltype(ClassType::class_definition)>{}, int>::type = 0>
			static JSValue class_make_inst(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst *argv);
			
			template <typename ClassType>
			static ClassType* raw_to_inst_ptr(ClassType* raw_ptr)
			{
				return raw_ptr;
			}
			
			template <typename ClassType>
			static ClassType* raw_to_inst_ptr(std::shared_ptr<ClassType>* raw_ptr)
			{
				return raw_ptr->get();
			}
			
			template <typename ClassType>
			static ClassType* raw_to_inst(ClassType* raw_ptr)
			{
				return raw_ptr;
			}
			
			template <typename ClassType>
			static std::shared_ptr<ClassType>& raw_to_inst(std::shared_ptr<ClassType>* raw_ptr)
			{
				return *raw_ptr;
			}
			
			template <typename ClassType, typename std::enable_if<std::is_base_of<class_def<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
			static void finalizer(JSRuntime *rt, JSValue val);
			
			template <typename ClassType, typename std::enable_if<std::is_base_of<class_def_shared<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
			static void finalizer(JSRuntime *rt, JSValue val);
			
			template <typename ClassType>
			static JSValue ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
			
			template <typename...>
			using void_t = void;
			
			template <typename, typename = void>
			struct has_gc_mark: std::false_type{};
			
			template <typename C>
			struct has_gc_mark<C, void_t<decltype(&C::gc_mark)>>: std::is_same<void, decltype(std::declval<C>().gc_mark(nullptr))>{};
			
			template <typename ClassType, typename std::enable_if<has_gc_mark<ClassType>::value, ClassType>::type* = nullptr>
			static void gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func);
			
			template <typename ClassType, typename std::enable_if<!has_gc_mark<ClassType>::value, ClassType>::type* = nullptr>
			static void gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
			{
			}
			
			template <typename ClassType, value(ClassType::*Func)(const args&)>
			static JSValue invoke_member(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
			
			template <typename ClassType, value(ClassType::*Getter)(const value&)>
			static JSValue invoke_getter(JSContext *ctx, JSValueConst this_val);
			
			template <typename ClassType, void(ClassType::*Getter)(const value&, const value&)>
			static JSValue invoke_setter(JSContext *ctx, JSValueConst this_val, JSValueConst val);
		};
		
		struct members
		{
			template <typename MemberType>
			static void add_function_list_entry(JSCFunctionListEntry* entries, size_t& idx, MemberType member);
			
			template <typename ClassType, typename ClassDefType, typename... Args>
			static void create_class_def(ClassDefType& cdef, const char* name, int ctor_argc, Args&&... args);
		};
	}
	
	class value:
		private detail::list_entry
	{
		friend class context;
		friend class runtime;
		friend class args;
		friend class context_exception;
		friend class detail::jsvalue_list;
		friend class detail::closures_common;
		friend class detail::functions;
		template <typename FirstType> friend class detail::closures;
		friend class detail::classes;
		template <typename Owned> friend class detail::owner;
		template <typename ClassType> friend class class_builder;
		
		context* owner_{nullptr};
		JSContext* ctx_{nullptr};
		JSValue val_{0}; // valid if ctx_ is not null
		
		void track();
		void track(value& from);
		void untrack();
		
		inline void validate() const
		{
			if (!valid())
				throw_value_exception("no context");
		}
		
		inline void throw_value_exception(const char* msg) const;
		
		inline void do_throw(value exval);
		
		void handle_pending_exception();
		
		inline void check_throw(bool check_exceptions)
		{
			if (ctx_)
			{
				if (check_exceptions)
					handle_pending_exception();
				if (JS_IsException(val_))
					do_throw(value(ctx_, JS_GetException(ctx_)));
			}
		}
		
		JSValue steal()
		{
			validate();
			JSValue ret = val_;
			val_ = {0};
			ctx_ = nullptr;
			return ret;
		}
		
		explicit value(JSContext* ctx, JSValue val, bool dup = false):
			ctx_(ctx),
			val_(ctx && dup ? JS_DupValue(ctx, val) : val)
		{
			QJSCPP_DEBUG("value(JSContext*, JSValue) @" << (void*)this);
			validate();
			track();
		}
		
	public:
		typedef std::function<void(JSValue val)> mark_func;
		
		value()
		{
			QJSCPP_DEBUG("value() @" << (void*)this);
		}
		
		value(value&& from):
			ctx_(from.ctx_),
			val_(from.val_)
		{
			QJSCPP_DEBUG("value(value&& @" << (void*)&from << ") @" << (void*)this << " -> " << (from.valid() ? from.as_cstring() : "[nothing]"));
			if (&from != this)
			{
				from.ctx_ = nullptr;
				from.val_ = {0};
				if (ctx_)
					track(from);
			}
		}
		
		explicit value(JSContext *ctx, const value& other)
		{
			QJSCPP_DEBUG("value(JSContext, const value& @" << (void*)&other << ") @" << (void*)this << " -> " << (other.valid() ? other.as_cstring() : "[nothing]"));
			if (ctx && other.ctx_)
			{
				ctx_ = ctx;
				val_ = JS_DupValue(ctx_, other.val_);
				track();
			}
			else
			{
				ctx_ = nullptr;
				val_ = {0};
			}
		}
		
		value(const value& other):
			ctx_(other.ctx_)
		{
			QJSCPP_DEBUG("value(const value& @" << (void*)&other << ") @" << (void*)this << " -> " << (other.valid() ? other.as_cstring() : "[nothing]"));
			if (ctx_)
			{
				track();
				val_ = JS_DupValue(ctx_, other.val_);
			}
		}
		
		value& operator=(const value& other)
		{
			QJSCPP_DEBUG("value& operator=(const value& @" << (void*)&other << ") @" << (void*)this << " -> " << (other.valid() ? other.as_cstring() : "[nothing]"));
			if (&other != this)
			{
				if (ctx_)
				{
					untrack();
					JS_FreeValue(ctx_, val_);
				}
				ctx_ = other.ctx_;
				if (ctx_)
				{
					track();
					val_ = JS_DupValue(ctx_, other.val_);
				}
				else
					val_ = {0};
			}
			return *this;
		}
		
		value& operator=(value&& from)
		{
			QJSCPP_DEBUG("value& operator=(value&& @" << (void*)&from << ") @" << (void*)this << " -> " << (from.valid() ? from.as_cstring() : "[nothing]"));
			if (&from != this)
			{
				if (ctx_)
				{
					JS_FreeValue(ctx_, val_);
					if (!from.ctx_)
						untrack();
				}
				else if (from.ctx_)
					track();
				ctx_ = from.ctx_;
				from.ctx_ = nullptr;
				val_ = from.val_;
				from.val_ = {0};
				if (ctx_)
					from.untrack();
			}
			return *this;
		}
		
		value(JSContext* ctx, const char* str):
			ctx_(ctx),
			val_(JS_NewStringLen(ctx_, str, ::strlen(str)))
		{
			QJSCPP_DEBUG("value(JSContext*, std::string) @" << (void*)this);
			validate();
			track();
		}
		
		value(JSContext* ctx, const std::string& str):
			ctx_(ctx),
			val_(JS_NewStringLen(ctx_, str.data(), str.length()))
		{
			QJSCPP_DEBUG("value(JSContext*, std::string) @" << (void*)this);
			validate();
			track();
		}
		
		template<typename R, typename... A>
		value(JSContext* ctx, R(*f)(A...)):
			ctx_(ctx)
		{
			QJSCPP_DEBUG("value(JSContext*, int, [function]) @" << (void*)this);
			validate();
			
			using traits = detail::func_traits<decltype(f)>;
			val_ = detail::closures<typename traits::template arg<0>::type>::template create(ctx_, f);
			check_throw(false);
			
			track();
		}
		
		template<typename R>
		value(JSContext* ctx, R(*f)()):
			ctx_(ctx)
		{
			QJSCPP_DEBUG("value(JSContext*, int, [function]) @" << (void*)this);
			validate();
			
			val_ = detail::closures<void>::template create(ctx_, f);
			check_throw(false);
			
			track();
		}
		
		template <typename Func>
		value(JSContext* ctx, Func f):
			ctx_(ctx)
		{
			QJSCPP_DEBUG("value(JSContext*, int, [functor]) @" << (void*)this);
			validate();
			
			using traits = detail::func_traits<decltype(f)>;
			val_ = detail::closures<typename traits::template arg<0>::type>::template create(ctx_, std::move(f));
			check_throw(false);
			
			track();
		}
		
		template <typename ClassType>
		value(JSContext* ctx, const std::shared_ptr<ClassType>& inst):
			ctx_(ctx)
		{
			QJSCPP_DEBUG("value(JSContext*, [object]) @" << (void*)this);
			validate();
			
			val_ = detail::classes::class_make_object_for_inst<ClassType>(ctx_, inst);
			check_throw(false);
			
			track();
		}
		
		virtual ~value()
		{
			QJSCPP_DEBUG("~value() @" << (void*)this);
			if (ctx_)
				JS_FreeValue(ctx_, val_);
			
			untrack();
		}
		
		bool valid() const
		{
			return ctx_ != nullptr;
		}
		
		void abandon()
		{
			if (ctx_)
			{
				QJSCPP_DEBUG("abandon value @" << (void*)this);
				JS_FreeValue(ctx_, val_);
			}
			if (owner_)
			{
				owner_ = nullptr;
				unlink();
			}
			ctx_ = nullptr;
			val_ = {0};
		}
		
		bool is_null() const
		{
			validate();
			return JS_IsNull(val_);
		}
		
		bool is_undefined() const
		{
			validate();
			return JS_IsUndefined(val_);
		}
		
		bool is_exception() const
		{
			validate();
			return JS_IsError(ctx_, val_);
		}
		
		bool is_function() const
		{
			validate();
			return JS_IsFunction(ctx_, val_);
		}
		
		bool is_number() const
		{
			validate();
			return JS_IsNumber(val_);
		}
		
		bool is_object() const
		{
			validate();
			return JS_IsObject(val_);
		}
		
		bool is_string() const
		{
			validate();
			return JS_IsString(val_);
		}
		
		bool is_bool() const
		{
			validate();
			return JS_IsBool(val_);
		}
		
		bool as_bool() const
		{
			validate();
			bool ret = false;
			if (!as_bool(ret))
				throw_value_exception("not a int32 value");
			return ret;
		}
		
		bool as_bool(bool& val) const
		{
			if (!valid())
			{
				val = false;
				return false;
			}
			int ret = JS_ToBool(ctx_, val_);
			if (ret < 0)
			{
				val = false;
				return false;
			}
			
			val = (ret != 0);
			return true;
		}
		
		double as_double() const
		{
			validate();
			double ret = 0;
			if (!as_double(ret))
				throw_value_exception("not a double value");
			return ret;
		}
		
		bool as_double(double& val) const
		{
			if (!valid() || JS_ToFloat64(ctx_, &val, val_) < 0)
			{
				val = 0.0;
				return false;
			}
			return true;
		}
		
		int32_t as_int32() const
		{
			validate();
			int32_t ret = 0;
			if (!as_int32(ret))
				throw_value_exception("not a int32 value");
			return ret;
		}
		
		bool as_int32(int32_t& val) const
		{
			if (!valid() || JS_ToInt32(ctx_, &val, val_) < 0)
			{
				val = 0;
				return false;
			}
			return true;
		}
		
		uint32_t as_uint32() const
		{
			validate();
			uint32_t ret = 0;
			if (!as_uint32(ret))
				throw_value_exception("not a uint32 value");
			return ret;
		}
		
		bool as_uint32(uint32_t& val) const
		{
			if (!valid() || JS_ToUint32(ctx_, &val, val_) < 0)
			{
				val = 0;
				return false;
			}
			return true;
		}
		
		int64_t as_int64() const
		{
			validate();
			int64_t ret = 0;
			if (!as_int64(ret))
				throw_value_exception("not a int64 value");
			return ret;
		}
		
		bool as_int64(int64_t& val) const
		{
			if (!valid() || JS_ToInt64(ctx_, &val, val_) < 0)
			{
				val = 0;
				return false;
			}
			return true;
		}
		
		std::string as_string() const
		{
			validate();
			std::string ret;
			if (!as_string(ret))
				throw_value_exception("not a string value");
			return ret;
		}
		
		bool as_string(std::string& val) const
		{
			if (valid())
			{
				if (auto cstr = as_cstring())
				{
					val = cstr.str();
					return true;
				}
			}
			
			return false;
		}
		
		inline static value undefined(JSContext* ctx)
		{
			return value(ctx, JS_UNDEFINED);
		}
		
		inline static value null(JSContext* ctx)
		{
			return value(ctx, JS_NULL);
		}
		
		template <typename T>
		inline static value exception(JSContext* ctx, T exval)
		{
			return value(ctx, JS_Throw(ctx, value(ctx, exval).steal()));
		}
		
		inline static value reference_error(JSContext* ctx, const std::string& str)
		{
			return value(ctx, JS_ThrowReferenceError(ctx, "%s", str.c_str()));
		}
		
		inline static value type_error(JSContext* ctx, const std::string& str)
		{
			return value(ctx, JS_ThrowTypeError(ctx, "%s", str.c_str()));
		}
		
		context& get_context() const
		{
			validate();
			return *reinterpret_cast<context*>(JS_GetContextOpaque(ctx_));
		}
		
		cstring as_cstring() const
		{
			validate();
			return cstring(ctx_, val_);
		}
		
		value get_property(const char* name) const
		{
			validate();
			return value(ctx_, JS_GetPropertyStr(ctx_, val_, name));
		}
		
		value get_property(const std::string& name) const
		{
			return get_property(name.c_str());
		}
		
		bool set_property(const char* name, value val)
		{
			validate();
			int ret = JS_SetPropertyStr(ctx_, val_, name, val.steal());
			if (ret < 0)
				do_throw(value(ctx_, JS_GetException(ctx_)));
			return ret;
		}
		
		bool set_property(const std::string& name, value val)
		{
			return set_property(name.c_str(), std::move(val));
		}
		
		template <typename R, typename... A>
		bool set_property(const char* name, R(*func)(A...))
		{
			validate();
			int ret = JS_SetPropertyStr(ctx_, val_, name, value(ctx_, func).steal());
			if (ret < 0)
				do_throw(value(ctx_, JS_GetException(ctx_)));
			return ret;
		}
		
		template <typename R, typename... A>
		bool set_property(const std::string& name, R(*func)(A...))
		{
			return set_property(name.c_str(), func);
		}
		
		template <typename Func>
		bool set_property(const char* name, Func func)
		{
			validate();
			int ret = JS_SetPropertyStr(ctx_, val_, name, value(ctx_, std::move(func)).steal());
			if (ret < 0)
				do_throw(value(ctx_, JS_GetException(ctx_)));
			return ret;
		}
		
		template <typename Func>
		bool set_property(const std::string& name, Func func)
		{
			return set_property(name.c_str(), func);
		}
		
		template <typename ... Args>
		value operator()(Args&& ... args) const;
		
		template <typename ... Args>
		value call(const value& thisObj, Args&&... args) const;
		
		template <typename... Args>
		value call_member(const char* name, Args&&... args)
		{
			validate();
			
			return get_property(name).call(*this, std::forward<Args>(args) ...);
		}
		
		template <typename... Args>
		value call_member(const std::string& name, Args&&... args)
		{
			return call_member(name.c_str(), std::forward<Args>(args)...);
		}
	};
	
	
	class args:
		public std::vector<value>
	{
		friend class value;
		friend class detail::classes;
		friend class detail::closures_common;
		
		value this_;
		
		args(JSContext* ctx, size_t N, JSValue this_obj, size_t argc, JSValueConst* argv):
			std::vector<value>((N > argc) ? N : argc),
			this_(ctx, this_obj, true)
		{
			size_t i = 0;
			for (; i < argc; i++)
				(*this)[i] = value(ctx, argv[i], true);
			for (; i < N; i++)
				(*this)[i] = value::undefined(ctx);
		}
		
	public:
		context& get_context() const
		{
			return this_.get_context();
		}
		
		const value& get_this() const
		{
			return this_;
		}
	};
	
	class throw_exception:
		public exception
	{
		value value_;
		
	public:
		throw_exception()
		{
		}
		
		throw_exception(value&& val):
			value_(std::move(val))
		{
		}
		
		value val() const
		{
			return value_;
		}
		
		virtual const char* what() const noexcept
		{
			return "thrown exception";
		}
	};
	
	class value_exception:
		public exception
	{
		friend class value;
		
		value value_;
		
		value_exception(value&& val):
			value_(std::move(val))
		{
		}
		
		template <typename T>
		value_exception(T val):
			exception(val)
		{
		}
		
		value_exception(const cstring& val):
			exception(val.str())
		{
		}
		
	public:
		value val() const
		{
			return value_;
		}
	};
	
	template <typename ClassType>
	class class_def
	{
		friend class runtime;
		friend class context;
		friend class detail::classes;
		friend class detail::members;
		
		JSClassID id{0};
		const char* name{nullptr};
		int ctor_argc{0};
		std::vector<JSCFunctionListEntry> members;
		
		class_def(size_t mem_cnt):
			members(mem_cnt)
		{
		}
	};
	
	template <typename ClassType>
	class class_def_shared
	{
		friend class runtime;
		friend class context;
		friend class detail::classes;
		friend class detail::members;
		
		JSClassID id{0};
		const char* name{nullptr};
		int ctor_argc{0};
		std::vector<JSCFunctionListEntry> members;
		
		class_def_shared(size_t mem_cnt):
			members(mem_cnt)
		{
		}
	};
	
	class context:
		private detail::list_entry
	{
		friend class runtime;
		friend class value;
		friend class detail::owner<context>;
		friend class detail::classes;
		friend class detail::closures_common;
		friend class detail::functions;
		
		class call_level
		{
		public:
			typedef unsigned int val_type;
			
			call_level(val_type& val):
				val_(val)
			{
				val_++;
				assert(val_ != 0);
			}
			
			~call_level()
			{
				assert(val_ > 0);
				val_--;
			}
			
		private:
			val_type& val_;
		};
		
		struct class_info
		{
			JSValue ctor;
			JSValue proto;
			
			class_info() = delete;
			class_info(const class_info&) = delete;
			class_info(class_info&&) = delete;
			class_info& operator=(const class_info&) = delete;
			class_info& operator=(class_info&&) = delete;
			
			class_info(JSValue ctor, JSValue proto):
				ctor(ctor),
				proto(proto)
			{
				assert(!JS_IsUndefined(ctor));
				assert(!JS_IsUndefined(proto));
			}
			
			~class_info()
			{
				assert(JS_IsUndefined(ctor));
				assert(JS_IsUndefined(proto));
			}
			
			void cleanup(JSContext* ctx)
			{
				JS_FreeValue(ctx, ctor);
				JS_FreeValue(ctx, proto);
				ctor = JS_UNDEFINED;
				proto = JS_UNDEFINED;
			}
		};
		
		std::unique_ptr<JSContext, decltype(&JS_FreeContext)> ctx_{nullptr, nullptr};
		runtime* owner_{nullptr};
		call_level::val_type clevel_{0};
		detail::owner<value> values_;
		std::exception_ptr excpt_;
		std::map<JSClassID, std::unique_ptr<class_info>> classes_;
		
		void cleanup_classes()
		{
			auto ctx = ctx_.get();
			for (auto const& it : classes_)
				it.second->cleanup(ctx);
		}
		
		const class_info& get_class_info(JSClassID id) const
		{
			auto it = classes_.find(id);
			if (it == classes_.end())
				throw exception("class not registered");
			return *it->second.get();
		}
		
		context(runtime* own, JSRuntime* rt):
			ctx_(JS_NewContext(rt), &JS_FreeContext),
			owner_(own)
		{
			QJSCPP_DEBUG("context @" << (void*)this);
			JS_SetContextOpaque(ctx_.get(), this);
			track();
		}
		
		void store_exception(std::exception_ptr excpt)
		{
			assert(!excpt_);
			excpt_ = excpt;
		}
		
		std::exception_ptr pop_exception()
		{
			auto ret = excpt_;
			excpt_ = nullptr;
			return ret;
		}
		
		
		void track();
		void track(context& from);
		void untrack();
		
		void abandon()
		{
			QJSCPP_DEBUG("context @" << (void*)this << " abandoned, delete ctx: " << (ctx_ ? "yes" : "no"));
			if (ctx_)
			{
				cleanup_classes();
				JS_SetContextOpaque(ctx_.get(), nullptr);
				ctx_.reset();
			}
			if (owner_)
			{
				unlink();
				owner_ = nullptr;
			}
		}
		
		inline void validate() const
		{
			if (!valid())
				throw invalid_context();
		}
		
	public:
		context() = default;
		context& operator=(const context&) = delete;
		
		context(context&& from):
			ctx_(std::move(from.ctx_)),
			owner_(from.owner_)
		{
			QJSCPP_DEBUG("context @" << (void*)this << " <- @" << (void*)&from);
			JS_SetContextOpaque(ctx_.get(), this);
			track(from);
		}
		
		context& operator=(context&& from)
		{
			if (&from != this)
			{
				QJSCPP_DEBUG("context @" << (void*)this << " <= @" << (void*)&from);
				ctx_ = std::move(from.ctx_);
				JS_SetContextOpaque(ctx_.get(), this);
				track(from);
			}
			return *this;
		}
		
		virtual ~context()
		{
			QJSCPP_DEBUG("~context @" << (void*)this);
			values_.for_each(
				[](value* val)
				{
					val->abandon();
				});
			untrack();
			if (ctx_)
			{
				cleanup_classes();
				JS_SetContextOpaque(ctx_.get(), nullptr);
			}
		}
		
		runtime& get_runtime() const
		{
			if (!ctx_)
				throw invalid_context();
			return *reinterpret_cast<runtime*>(JS_GetRuntimeOpaque(JS_GetRuntime(ctx_.get())));
		}
		
		bool valid() const
		{
			return owner_ != nullptr && ctx_;
		}
		
		operator JSContext*() const
		{
			validate();
			return ctx_.get();
		}
		
		enum class eval_flags
		{
			global,
			module,
			autodetect
		};
		
		value get_global_object() const
		{
			validate();
			auto ctx = ctx_.get();
			return value(ctx, JS_GetGlobalObject(ctx));
		}
		
		value eval(const char* str, eval_flags flags = eval_flags::autodetect)
		{
			return eval(str, ::strlen(str), flags);
		}
		
		value eval(const char* buf, size_t len, eval_flags flags = eval_flags::autodetect, const char* filename = nullptr)
		{
			validate();
			
			int eval_flags;
			switch (flags)
			{
				case eval_flags::global:
					eval_flags = JS_EVAL_TYPE_GLOBAL;
					break;
				case eval_flags::module:
					eval_flags = JS_EVAL_TYPE_MODULE;
					break;
				case eval_flags::autodetect:
					eval_flags = JS_DetectModule(buf, len) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
					break;
			}
			
			auto ctx = ctx_.get();
			value ret(ctx, JS_Eval(ctx, buf, len, (filename && filename[0]) ? filename : "(none)", eval_flags));
			ret.check_throw(true);
			return ret;
		}
		
		template <typename... Args>
		value call_global(const char* name, Args&&... args)
		{
			validate();
			
			return get_global_object().get_property(name)(value::undefined(ctx_.get()), std::forward<Args>(args) ...);
		}
		
		template <typename... Args>
		value call_global(const std::string& name, Args&&... args)
		{
			return call_global(name.c_str(), std::forward<Args>(args)...);
		}
		
		template <typename ClassType>
		void register_class()
		{
			validate();
			
			assert(ClassType::class_definition.id != 0);
			
			auto ctx = ctx_.get();
			auto rt = JS_GetRuntime(ctx_.get());
			if (!JS_IsRegisteredClass(rt, ClassType::class_definition.id))
			{
				JSClassDef cdef{
					.class_name = ClassType::class_definition.name,
					.finalizer = detail::classes::finalizer<ClassType>,
					.gc_mark = detail::classes::gc_mark<ClassType>
				};
				if (JS_NewClass(rt, ClassType::class_definition.id, &cdef) < 0)
					throw exception("failed to register class");
			}
			
			QJSCPP_DEBUG("Registering class '" << ClassType::class_definition.name << "' with id: " << ClassType::class_definition.id);
			
			value proto(ctx, JS_NewObject(ctx));
			if (!ClassType::class_definition.members.empty())
				JS_SetPropertyFunctionList(ctx, proto.val_, &ClassType::class_definition.members[0], ClassType::class_definition.members.size());
			JS_SetClassProto(ctx, ClassType::class_definition.id, proto.val_);
			value ctor(ctx, JS_NewCFunction2(ctx, detail::classes::ctor<ClassType>, ClassType::class_definition.name, ClassType::class_definition.ctor_argc, JS_CFUNC_constructor, 0));
			if (!ctor.valid() || ctor.is_exception())
				throw exception("failed to create class constructor");
			JS_SetConstructor(ctx, ctor.val_, value(ctx, proto.val_, true).steal());
			get_global_object().set_property(ClassType::class_definition.name, ctor); // TODO: is this the right way?
			classes_.insert(std::make_pair(ClassType::class_definition.id, new class_info(ctor.steal(), proto.steal())));
		}
		
		template <typename ClassType, typename InstanceType>
		value make_object(const std::vector<value>& args, InstanceType& inst) const
		{
			validate();
			
			auto const& info = get_class_info(ClassType::class_definition.id);
			
			std::vector<JSValue> a(args.size());
			for (size_t i = 0; i < args.size(); i++)
				a[i] = args[i].val_;
			
			auto ctx = ctx_.get();
			value ret(ctx, JS_CallConstructor(ctx, info.ctor, a.size(), !a.empty() ? &a[0] : nullptr));
			ret.check_throw(true);
			if (ret.valid() && !ret.is_exception())
				inst = detail::classes::get_inst<ClassType>(ret.val_);
			else
				inst = {};
			return ret;
		}
	};
	
	template <typename ClassType>
	class object
	{
		friend class detail::members;
		
		JSCFunctionListEntry entry_;
		
		object(JSCFunctionListEntry&& entry):
			entry_(std::move(entry))
		{
		}
		
	public:
		template <value(ClassType::*Func)(const quickjs::args&)>
		static object function(const char* name, uint8_t nargs = 0)
		{
			auto func = detail::classes::invoke_member<ClassType, Func>;
			return object(JSCFunctionListEntry(JS_CFUNC_DEF(name, nargs, func)));
		}
		
		template <value(ClassType::*Getter)(const quickjs::value&), void(ClassType::*Setter)(const quickjs::value&, const quickjs::value&)>
		static object getset(const char* name)
		{
			auto getter = detail::classes::invoke_getter<ClassType, Getter>;
			auto setter = detail::classes::invoke_setter<ClassType, Setter>;
			return object(JSCFunctionListEntry(JS_CGETSET_DEF(name, getter, setter)));
		}
		
		template <value(ClassType::*Getter)(const quickjs::value&)>
		static object get_only(const char* name)
		{
			auto getter = detail::classes::invoke_getter<ClassType, Getter>;
			auto setter =
				[](JSContext *ctx, JSValueConst /*this_val*/, JSValueConst /*val*/) -> JSValue
				{
					return JS_ThrowTypeError(ctx, "property is read-only");
				};
			return object(JSCFunctionListEntry(JS_CGETSET_DEF(name, getter, setter)));
		}
		
		template <void(ClassType::*Setter)(const quickjs::value&, const quickjs::value&)>
		static object set_only(const char* name)
		{
			auto getter =
				[](JSContext *ctx, JSValueConst /*this_val*/) -> JSValue
				{
					return JS_ThrowTypeError(ctx, "property is write-only");
				};
			auto setter = detail::classes::invoke_setter<ClassType, Setter>;
			return object(JSCFunctionListEntry(JS_CGETSET_DEF(name, getter, setter)));
		}
	};
	
	namespace detail
	{
		template <typename MemberType>
		inline void members::add_function_list_entry(JSCFunctionListEntry* entries, size_t& idx, MemberType member)
		{
			entries[idx++] = member.entry_;
		}
		
		template <typename ClassType, typename ClassDefType, typename... Args>
		inline void members::create_class_def(ClassDefType& cdef, const char* name, int ctor_argc, Args&&... args)
		{
			JS_NewClassID(&cdef.id);
			cdef.name = name;
			cdef.ctor_argc = ctor_argc;
			
			if (sizeof...(args) > 0)
			{
				size_t ecnt = 0;
				
				// C++17 has fold expressions...
				__attribute__((unused)) int dummy[] = {{0}, ((void)members::add_function_list_entry(&cdef.members[0], ecnt, std::forward<Args>(args)), 0)... };
				assert(ecnt == sizeof...(args));
			}
		}
	}
	
	class runtime
	{
		friend class context;
		friend class value;
		friend class detail::classes;
		
		JSMallocFunctions mf_{
			[](JSMallocState *s, size_t size) -> void*
			{
				return reinterpret_cast<runtime*>(s->opaque)->js_malloc(s, size);
			},
			[](JSMallocState *s, void *ptr)
			{
				reinterpret_cast<runtime*>(s->opaque)->js_free(s, ptr);
			},
			[](JSMallocState *s, void *ptr, size_t size) -> void*
			{
				return reinterpret_cast<runtime*>(s->opaque)->js_realloc(s, ptr, size);
			},
			nullptr
		};
		std::unique_ptr<JSRuntime, decltype(&JS_FreeRuntime)> rt_;
		detail::owner<context> contexts_;
		
		struct inst_ref
		{
			size_t refs_{0};
			JSValue weak_val_;
			
			inst_ref() = delete;
			
			inst_ref(JSValue weak_val):
				weak_val_(weak_val)
			{
			}
			
			inst_ref(inst_ref&&) = default;
			inst_ref& operator=(inst_ref&&) = default;
			
			inst_ref(const inst_ref&) = delete;
			inst_ref& operator=(const inst_ref&) = delete;
			
			inline void ref()
			{
				refs_++;
			}
			
			inline bool unref()
			{
				return --refs_ == 0;
			}
		};
		std::map<void*, inst_ref> weak_object_refs_;
		
		inline void ref_inst_value(void* inst, JSValue weak_val)
		{
			weak_object_refs_.insert(std::make_pair(inst, inst_ref(weak_val))).first->second.ref();
		}
		
		inline void unref_inst_value(void* inst)
		{
			auto it = weak_object_refs_.find(inst);
			if (it->second.unref())
				weak_object_refs_.erase(it);
		}
		
		inline bool get_inst_value(JSContext* ctx, void* inst, JSValue& ref) const
		{
			auto it = weak_object_refs_.find(inst);
			if (it != weak_object_refs_.end())
			{
				ref = JS_DupValue(ctx, it->second.weak_val_);
				return true;
			}
			return false;
		}
	
	protected:
		virtual void* js_malloc(JSMallocState *, size_t size)
		{
			return ::malloc(size);
		}
		
		virtual void js_free(JSMallocState *, void *ptr)
		{
			::free(ptr);
		}
		
		virtual void* js_realloc(JSMallocState *, void *ptr, size_t size)
		{
			return ::realloc(ptr, size);
		}
		
	public:
		runtime(const runtime&) = delete;
		runtime(runtime&&) = delete;
		runtime& operator=(const runtime&) = delete;
		runtime& operator=(runtime&&) = delete;
		
		runtime(bool enableMemoryHooks = false):
			rt_(enableMemoryHooks ? JS_NewRuntime2(&mf_, this) : JS_NewRuntime(), &::JS_FreeRuntime)
		{
			QJSCPP_DEBUG("runtime @" << (void*)this);
			JS_SetRuntimeOpaque(rt_.get(), this);
		}
		
		virtual ~runtime()
		{
			QJSCPP_DEBUG("~runtime @" << (void*)this);
			contexts_.for_each(
				[](context *ctx)
				{
					ctx->abandon();
				});
			JS_SetRuntimeOpaque(rt_.get(), nullptr);
		}
		
		operator JSRuntime*() const { return rt_.get(); }
		
		context new_context()
		{
			return context(this, rt_.get());
		}
		
		void run_gc() const
		{
			JS_RunGC(rt_.get());
		}
		
		template <typename ClassType, typename... Args>
		static class_def<ClassType> create_class_def(const char* name, int ctor_argc = 0, Args&&... args)
		{
			class_def<ClassType> cdef(sizeof...(args));
			detail::members::create_class_def<ClassType>(cdef,name, ctor_argc, std::forward<Args>(args)...);
			return cdef;
		}
		
		template <typename ClassType, typename... Args>
		static class_def_shared<ClassType> create_class_def_shared(const char* name, int ctor_argc = 0, Args&&... args)
		{
			class_def_shared<ClassType> cdef(sizeof...(args));
			detail::members::create_class_def<ClassType>(cdef,name, ctor_argc, std::forward<Args>(args)...);
			return cdef;
		}
	};
	
	inline void value::throw_value_exception(const char* msg) const
	{
		throw value_exception(msg);
	}
	
	inline void value::do_throw(value exval)
	{
			if (JS_IsError(ctx_, val_))
			{
				value stack = exval.get_property("stack");
				throw value_error(exval.as_cstring(), !stack.is_undefined() ? stack.as_cstring() : cstring());
			}
			else
			{
				context& c = *reinterpret_cast<context*>(JS_GetContextOpaque(ctx_));
				if (c.clevel_ > 1)
				{
					QJSCPP_DEBUG("do_throw: clevel: " << c.clevel_ << ": throwing internal throw_exception");
					throw throw_exception(std::move(exval));
				}
				else
				{
					QJSCPP_DEBUG("do_throw: throwing value_exception");
					throw value_exception(std::move(exval));
				}
			}
	}
	
	inline void value::handle_pending_exception()
	{
		if (auto excpt = get_context().pop_exception())
		{
			QJSCPP_DEBUG("value @" << (void*)this << ": rethrow forwarded exception");
			std::rethrow_exception(excpt);
		}
	}
	
	inline void value::track()
	{
		if (!owner_ && ctx_)
		{
			owner_ = &get_context();
			QJSCPP_DEBUG("value track() @" << (void*)this << ": add");
			owner_->values_.insert_head(*this);
		}
	}
	
	inline void value::track(value& from)
	{
		if (from.owner_)
		{
			QJSCPP_DEBUG("value track(value&) @" << (void*)this << ": abandon: @" << (void*)&from);
			from.untrack();
			from.owner_ = nullptr;
		}
		if (owner_)
		{
			QJSCPP_DEBUG("value track(value&) @" << (void*)this << ": remove");
			unlink();
			owner_ = nullptr;
		}
		if (ctx_)
		{
			QJSCPP_DEBUG("value track(value&) @" << (void*)this << ": add");
			owner_ = &get_context();
			owner_->values_.insert_head(*this);
		}
	}
	
	inline void value::untrack()
	{
		if (owner_)
		{
			QJSCPP_DEBUG("value untrack() @" << (void*)this << ": remove");
			unlink();
			owner_ = nullptr;
		}
	}
	
	template <typename... Args>
	inline value value::operator()(Args&&... a) const
	{
		validate();
		
		return detail::functions::call(*this, ctx_, value::undefined(ctx_), std::forward<Args>(a)...);
	}
	
	template <typename ... Args>
	value value::call(const value& thisObj, Args&&... a) const
	{
		validate();
		
		return detail::functions::call(*this, ctx_, thisObj, std::forward<Args>(a)...);
	}
	
	namespace detail
	{
		struct jsvalue_list
		{
			JSContext* ctx_;
			JSValue* vals_;
			size_t& cnt_;
			
			jsvalue_list(JSContext* ctx, JSValue* vals, size_t& cnt):
				ctx_(ctx),
				vals_(vals),
				cnt_(cnt)
			{
			}
			
			~jsvalue_list()
			{
				QJSCPP_DEBUG("jsvalue_list: Freeing values: " << cnt_);
				for (size_t i = 0; i < cnt_; i++)
					JS_FreeValue(ctx_, vals_[i]);
			}
			
			template <typename T>
			void add_value(T arg) const
			{
				vals_[cnt_++] = new_jsvalue(arg);
			}
			
			template <typename Begin, typename End>
			void add_values(Begin begin, End end)
			{
				for (auto it = begin; it != end; ++it)
					vals_[cnt_++] = new_jsvalue(*it);
			}
			
			inline JSValue new_jsvalue(const value& val) const
			{
				if (val.valid())
					return JS_DupValue(ctx_, val.val_);
				else
					return JS_UNDEFINED;
			}
			
			inline JSValue new_jsvalue(const char* str) const
			{
				return JS_NewString(ctx_, str);
			}
			
			inline JSValue new_jsvalue(const std::string& str) const
			{
				return JS_NewStringLen(ctx_, str.data(), str.length());
			}
			
			inline JSValue new_jsvalue(bool val) const
			{
				return val ? JS_TRUE : JS_FALSE;
			}
			
			inline JSValue new_jsvalue(int32_t val) const
			{
				return JS_NewInt32(ctx_, val);
			}
			
			inline JSValue new_jsvalue(uint32_t val) const
			{
				return JS_NewInt32(ctx_, static_cast<int32_t>(val));
			}
			
			inline JSValue new_jsvalue(int64_t val) const
			{
				return JS_NewInt64(ctx_, val);
			}
			
			inline JSValue new_jsvalue(double val) const
			{
				return JS_NewFloat64(ctx_, val);
			}
			
			template <typename Func>
			inline JSValue new_jsvalue(Func func) const
			{
				value v(ctx_, std::move(func));
				return v.valid() ? v.steal() : JS_UNDEFINED;
			}
			
			template<typename R, typename... A>
			inline JSValue new_jsvalue(R(*f)(A...)) const
			{
				value v(ctx_, f);
				return v.valid() ? v.steal() : JS_UNDEFINED;
			}
		};
	}
	
	namespace detail
	{
		//
		// classes
		//
		
		template <typename ClassType, typename std::enable_if<std::is_base_of<class_def<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
		inline ClassType* classes::get_raw_inst(JSValueConst val)
		{
			return reinterpret_cast<ClassType*>(JS_GetOpaque(val, ClassType::class_definition.id));
		}
		
		template <typename ClassType, typename std::enable_if<std::is_base_of<class_def_shared<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
		inline std::shared_ptr<ClassType>* classes::get_raw_inst(JSValueConst val)
		{
			return reinterpret_cast<std::shared_ptr<ClassType>*>(JS_GetOpaque(val, ClassType::class_definition.id));
		}
		
		template <typename ClassType, typename std::enable_if<std::is_base_of<class_def<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
		inline ClassType* classes::get_inst(JSValueConst val)
		{
			return reinterpret_cast<ClassType*>(JS_GetOpaque(val, ClassType::class_definition.id));
		}
		
		template <typename ClassType, typename std::enable_if<std::is_base_of<class_def_shared<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
		inline std::shared_ptr<ClassType> classes::get_inst(JSValueConst val)
		{
			return *reinterpret_cast<std::shared_ptr<ClassType>*>(JS_GetOpaque(val, ClassType::class_definition.id));
		}
		
		template <typename ClassType, typename std::enable_if<std::is_base_of<class_def<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
		inline void classes::finalizer(JSRuntime *rt, JSValue val)
		{
			// raw may be nullptr if the instantiation failed, e.g. an exception was thrown
			if (auto raw = get_raw_inst<ClassType>(val))
			{
				QJSCPP_DEBUG("Finalize class @ " << (void*)raw_to_inst_ptr(raw));
				
				delete raw;
			}
		}
		
		template <typename ClassType, typename std::enable_if<std::is_base_of<class_def_shared<ClassType>, decltype(ClassType::class_definition)>::value>::type* = nullptr>
		inline void classes::finalizer(JSRuntime *rt, JSValue val)
		{
			// raw may be nullptr if the instantiation failed, e.g. an exception was thrown
			if (auto raw = get_raw_inst<ClassType>(val))
			{
				auto inst = raw_to_inst_ptr(raw);
				QJSCPP_DEBUG("Finalize class @ " << (void*)inst);
				
				delete raw;
				
				runtime* r = reinterpret_cast<runtime*>(JS_GetRuntimeOpaque(rt));
				if (r)
				{
					// r will be reset to nullptr before freeing the runtime
					r->unref_inst_value(inst);
				}
			}
		}
		
		template <typename ClassType, typename std::enable_if<classes::has_gc_mark<ClassType>::value, ClassType>::type* = nullptr>
		inline void classes::gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
		{
			// raw may be nullptr if the instantiation failed, e.g. an exception was thrown
			if (auto raw = get_raw_inst<ClassType>(val))
			{
				QJSCPP_DEBUG("Mark class @ " << (void*)raw_to_inst_ptr(raw));
				raw_to_inst(raw)->gc_mark(
					[&](JSValue val)
					{
						JS_MarkValue(rt, val, mark_func);
					});
			}
		}
		
		template <typename ClassType, typename std::enable_if<std::is_base_of<class_def_shared<ClassType>, decltype(ClassType::class_definition)>{}, int>::type = 0>
		inline JSValue classes::class_make_object_for_inst(JSContext* ctx, const std::shared_ptr<ClassType>& inst)
		{
			runtime& r = *reinterpret_cast<runtime*>(JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
			
			JSValue ref;
			if (r.get_inst_value(ctx, inst.get(), ref))
				return ref;
			
			context& c = *reinterpret_cast<context*>(JS_GetContextOpaque(ctx));
			auto const& info = c.get_class_info(ClassType::class_definition.id);
			value obj(ctx, JS_NewObjectProtoClass(ctx, value(ctx, info.proto).steal(), ClassType::class_definition.id));
			r.ref_inst_value(inst.get(), obj.val_); // We don't want to hold a strong reference!
			JS_SetOpaque(obj.val_, new std::shared_ptr<ClassType>(inst));
			return obj.steal();
		}
		
		template <typename ClassType, typename std::enable_if<std::is_base_of<class_def<ClassType>, decltype(ClassType::class_definition)>{}, int>::type = 0>
		inline JSValue classes::class_make_inst(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst *argv)
		{
			value target(ctx, new_target, true);
			auto proto = target.get_property("prototype");
			if (!proto.valid())
				return JS_EXCEPTION;
			if (proto.is_exception())
				return proto.steal();
			value obj(ctx, JS_NewObjectProtoClass(ctx, proto.val_, ClassType::class_definition.id));
			if (!obj.valid())
				return JS_EXCEPTION;
			if (!obj.is_exception())
			{
				args a(ctx, ClassType::class_definition.ctor_argc, obj.val_, argc, argv);
				std::unique_ptr<ClassType> raw(new ClassType(a));
				JS_SetOpaque(obj.val_, raw.release());
			}
			return obj.steal();
		}
		
		template <typename ClassType, typename std::enable_if<std::is_base_of<class_def_shared<ClassType>, decltype(ClassType::class_definition)>{}, int>::type = 0>
		inline JSValue classes::class_make_inst(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst *argv)
		{
			value target(ctx, new_target, true);
			auto proto = target.get_property("prototype");
			if (!proto.valid())
				return JS_EXCEPTION;
			if (proto.is_exception())
				return proto.steal();
			value obj(ctx, JS_NewObjectProtoClass(ctx, proto.val_, ClassType::class_definition.id));
			if (!obj.valid())
				return JS_EXCEPTION;
			if (!obj.is_exception())
			{
				args a(ctx, ClassType::class_definition.ctor_argc, obj.val_, argc, argv);
				std::unique_ptr<std::shared_ptr<ClassType>> raw(new std::shared_ptr<ClassType>(std::make_shared<ClassType>(a)));
				runtime& r = *reinterpret_cast<runtime*>(JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
				r.ref_inst_value(raw->get(), obj.val_); // We don't want to hold a strong reference!
				JS_SetOpaque(obj.val_, raw.release());
			}
			return obj.steal();
		}
		
		template <typename ClassType>
		inline JSValue classes::ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
		{
			try
			{
				QJSCPP_DEBUG("ctor: calling with arguments: " << argc);
				return class_make_inst<ClassType>(ctx, new_target, argc, argv);
			}
			catch (const throw_exception& e)
			{
				QJSCPP_DEBUG("ctor: throw js exception");
				return JS_Throw(ctx, e.val().steal());
			}
			catch (...)
			{
				context& c = *reinterpret_cast<context*>(JS_GetContextOpaque(ctx));
				QJSCPP_DEBUG("ctor: forward exception");
				c.store_exception(std::current_exception());
				return JS_Throw(ctx, JS_NewUncatchableError(ctx));
			}
		}
		
		template <typename ClassType, value(ClassType::*Func)(const args&)>
		inline JSValue classes::invoke_member(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
		{
			QJSCPP_DEBUG("invoke_member with arguments: " << argc);
			if (auto raw = get_raw_inst<ClassType>(this_val))
			{
				auto inst = raw_to_inst_ptr(raw);
				QJSCPP_DEBUG("Call object member @ " << (void*)inst);
				
				try
				{
					args a(ctx, 0, this_val, argc, argv);
					value ret = (inst->*Func)(a);
					return ret.valid() ? ret.steal() : JS_UNDEFINED;
				}
				catch (const throw_exception& e)
				{
					QJSCPP_DEBUG("object member @ " << (void*)raw_to_inst_ptr(raw) << ": throw js exception");
					return JS_Throw(ctx, e.val().steal());
				}
				catch (...)
				{
					QJSCPP_DEBUG("object member @ " << (void*)raw_to_inst_ptr(raw) << ": forward exception");
					context& c = *reinterpret_cast<context*>(JS_GetContextOpaque(ctx));
					c.store_exception(std::current_exception());
					return JS_Throw(ctx, JS_NewUncatchableError(ctx));
				}
			}
			return JS_EXCEPTION;
		}
		
		template <typename ClassType, value(ClassType::*Getter)(const value&)>
		inline JSValue classes::invoke_getter(JSContext *ctx, JSValueConst this_val)
		{
			if (auto raw = get_raw_inst<ClassType>(this_val))
			{
				auto inst = raw_to_inst_ptr(raw);
				QJSCPP_DEBUG("Call object getter @ " << (void*)inst);
				try
				{
					auto ret = (inst->*Getter)(value(ctx, this_val, true));
					return ret.valid() ? ret.steal() : JS_UNDEFINED;
				}
				catch (const throw_exception& e)
				{
					QJSCPP_DEBUG("object getter @ " << (void*)raw_to_inst_ptr(raw) << ": throw js exception");
					return JS_Throw(ctx, e.val().steal());
				}
				catch (...)
				{
					QJSCPP_DEBUG("object getter @ " << (void*)raw_to_inst_ptr(raw) << ": forward exception");
					context& c = *reinterpret_cast<context*>(JS_GetContextOpaque(ctx));
					c.store_exception(std::current_exception());
					return JS_Throw(ctx, JS_NewUncatchableError(ctx));
				}
			}
			return JS_EXCEPTION;
		}
		
		template <typename ClassType, void(ClassType::*Setter)(const value&, const value&)>
		inline JSValue classes::invoke_setter(JSContext *ctx, JSValueConst this_val, JSValueConst val)
		{
			if (auto raw = get_raw_inst<ClassType>(this_val))
			{
				auto inst = raw_to_inst_ptr(raw);
				QJSCPP_DEBUG("Call object setter @ " << (void*)inst);
				try
				{
					(inst->*Setter)(value(ctx, this_val, true), value(ctx, val, true));
					return JS_UNDEFINED;
				}
				catch (const throw_exception& e)
				{
					QJSCPP_DEBUG("object setter @ " << (void*)raw_to_inst_ptr(raw) << ": throw js exception");
					return JS_Throw(ctx, e.val().steal());
				}
				catch (...)
				{
					QJSCPP_DEBUG("object setter @ " << (void*)raw_to_inst_ptr(raw) << ": forward exception");
					context& c = *reinterpret_cast<context*>(JS_GetContextOpaque(ctx));
					c.store_exception(std::current_exception());
					return JS_Throw(ctx, JS_NewUncatchableError(ctx));
				}
			}
			return JS_EXCEPTION;
		}
		
		//
		// functions
		//
		
		inline value functions::call(const value& func, JSContext* ctx, const value& thisObj)
		{
			return call_common(func, ctx, thisObj);
		}
		
		template <typename A>
		inline value functions::call(const value& func, JSContext* ctx, const value& thisObj, A&& a)
		{
			return call_common(func, ctx, thisObj, std::forward<A>(a));
		}
		
		template <typename A1, typename A2, typename... Args,
			typename = typename std::enable_if<!is_iterator_args<A1, A2>::value>::type>
		inline value functions::call(const value& func, JSContext* ctx, const value& thisObj, A1&& a1, A2&& a2, Args&&... a)
		{
			return call_common(func, ctx, thisObj, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<Args>(a)...);
		}

		template <typename Begin, typename End,
			typename = typename std::enable_if<is_iterator_args<Begin, End>::value>::type>
		inline value functions::call(const value& func, JSContext* ctx, const value& thisObj, Begin&& begin, End&& end)
		{
			return call_common_it(func, ctx, thisObj, std::forward<Begin>(begin), std::forward<End>(end));
		}
		
		inline value functions::call_common_args(const value& func, JSContext* ctx, const value& thisObj, size_t acnt, JSValue* avals)
		{
			context* c = reinterpret_cast<context*>(JS_GetContextOpaque(ctx));
			context::call_level cl(c->clevel_);
			value ret(ctx, JS_Call(ctx, func.val_, thisObj.valid() ? thisObj.val_ : JS_UNDEFINED, acnt, avals));
			ret.check_throw(true);
			return ret;
		}
		
		template <typename... Args>
		inline value functions::call_common(const value& func, JSContext* ctx, const value& thisObj, Args&&... a)
		{
			size_t acnt = 0;
			JSValue avals[sizeof...(a)];
			
			jsvalue_list alist(ctx, avals, acnt);
			
			// C++17 has fold expressions...
			__attribute__((unused)) int dummy[] = {{0}, ((void)alist.add_value(std::forward<Args>(a)), 0)... };
			assert(acnt == sizeof...(a));
			
			return call_common_args(func, ctx, thisObj, acnt, avals);
		}
		
		template <typename Begin, typename End>
		inline value functions::call_common_it(const value& func, JSContext* ctx, const value& thisObj, Begin&& begin, End&& end)
		{
			size_t acnt = 0;
			std::vector<JSValue> avals(std::distance(begin, end));
			
			jsvalue_list alist(ctx, &avals[0], acnt);
			alist.add_values(begin, end);
			assert(acnt == avals.size());
			
			return call_common_args(func, ctx, thisObj, acnt, acnt > 0 ? &avals[0] : nullptr);
		}
		
		//
		// values
		//
		namespace values
		{
			struct convert
			{
				const value& val_;
				
				convert(const value& val):
					val_(val)
				{
				}
				
				operator const value&() const
				{
					QJSCPP_DEBUG("passing through value @ " << (void*)&val_ << ": " << (val_.valid() ? val_.as_string() : ""));
					return val_;
				}
				
				operator std::string() const
				{
					QJSCPP_DEBUG("converting value @ " << (void*)&val_ << " to std::string" << ": " << ((val_.is_string() && val_.valid()) ? val_.as_string() : ""));
					std::string ret;
					return (val_.is_string() && val_.as_string(ret)) ? ret : std::string();
				}
				
				operator int32_t() const
				{
					QJSCPP_DEBUG("converting value @ " << (void*)&val_ << " to int32_t" << ": " << (val_.valid() ? val_.as_string() : ""));
					int32_t ret;
					return val_.as_int32(ret) ? ret : 0;
				}
				
				operator uint32_t() const
				{
					QJSCPP_DEBUG("converting value @ " << (void*)&val_ << " to uint32_t" << ": " << (val_.valid() ? val_.as_string() : ""));
					uint32_t ret;
					return val_.as_uint32(ret) ? ret : 0;
				}
				
				operator int64_t() const
				{
					QJSCPP_DEBUG("converting value @ " << (void*)&val_ << " to int64_t" << ": " << (val_.valid() ? val_.as_string() : ""));
					int64_t ret;
					return val_.as_int64(ret) ? ret : 0;
				}
				
				operator double() const
				{
					QJSCPP_DEBUG("converting value @ " << (void*)&val_ << " to double" << ": " << (val_.valid() ? val_.as_string() : ""));
					double ret;
					return val_.as_double(ret) ? ret : 0;
				}
				
				operator bool() const
				{
					QJSCPP_DEBUG("converting value @ " << (void*)&val_ << " to bool" << ": " << (val_.valid() ? val_.as_string() : ""));
					bool ret;
					return val_.as_bool(ret) ? ret : false;
				}
			};
		};
		
		//
		// closures
		//
		
		template<typename Func, size_t... Is, typename std::enable_if<!std::is_void<typename func_traits<Func>::result_type>::value>::type* = nullptr>
		inline auto handle_closure_expand_helper(Func f, const args& a, indices<Is...>)
			-> typename func_traits<decltype(f)>::result_type
		{
			return f((values::convert(a[Is]))...);
		}
		
		template<typename Func, size_t... Is, typename std::enable_if<std::is_void<typename func_traits<Func>::result_type>::value>::type* = nullptr>
		inline value handle_closure_expand_helper(Func f, const args& a, indices<Is...>)
		{
			f((values::convert(a[Is]))...);
			return {};
		}
		
		template <typename Func, size_t N>
		inline JSValue closures_common::handle_closure_expand(Func f, JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
		{
			context* c = reinterpret_cast<context*>(JS_GetContextOpaque(ctx));
			try
			{
				args a(ctx, N, this_val, argc, argv);

				QJSCPP_DEBUG("closure: got " << argc << " argument(s), expanding: " << N);
				value ret(ctx, handle_closure_expand_helper(f, a, typename make_indices<N>::type()));
				QJSCPP_DEBUG("closure: returned: " << (ret.valid() ? ret.as_cstring() : "[nothing]"));
				ret.check_throw(true);
				return ret.valid() ? ret.steal() : JS_UNDEFINED;
			}
			catch (const throw_exception& e)
			{
				QJSCPP_DEBUG("closure: throw js exception");
				return JS_Throw(ctx, e.val().steal());
			}
			catch (...)
			{
				QJSCPP_DEBUG("closure: forward exception");
				c->store_exception(std::current_exception());
				return JS_Throw(ctx, JS_NewUncatchableError(ctx));
			}
		}
		
		template<typename Func, size_t... Is, typename std::enable_if<!std::is_void<typename func_traits<Func>::result_type>::value>::type* = nullptr>
		inline auto handle_closure_expand_with_args_helper(Func f, const args& a, indices<Is...>)
			-> typename func_traits<decltype(f)>::result_type
		{
			return f(a, (values::convert(a[Is]))...);
		}
		
		template<typename Func, size_t... Is, typename std::enable_if<std::is_void<typename func_traits<Func>::result_type>::value>::type* = nullptr>
		inline value handle_closure_expand_with_args_helper(Func f, const args& a, indices<Is...>)
		{
			f(a, (values::convert(a[Is]))...);
			return {};
		}
		
		template <typename Func, size_t N>
		inline JSValue closures_common::handle_closure_expand_with_args(Func f, JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
		{
			context* c = reinterpret_cast<context*>(JS_GetContextOpaque(ctx));
			try
			{
				args a(ctx, N, this_val, argc, argv);

				QJSCPP_DEBUG("closure(args): got " << argc << " argument(s), expanding: " << N);
				value ret(ctx, handle_closure_expand_with_args_helper(f, a, typename make_indices<N>::type()));
				QJSCPP_DEBUG("closure(args): returned: " << (ret.valid() ? ret.as_cstring() : "[nothing]"));
				ret.check_throw(true);
				return ret.valid() ? ret.steal() : JS_UNDEFINED;
			}
			catch (const throw_exception& e)
			{
				QJSCPP_DEBUG("closure(args): throw js exception");
				return JS_Throw(ctx, e.val().steal());
			}
			catch (...)
			{
				QJSCPP_DEBUG("closure(args): forward exception");
				c->store_exception(std::current_exception());
				return JS_Throw(ctx, JS_NewUncatchableError(ctx));
			}
		}
	}
	
	//
	// context
	//
	
	inline void context::track()
	{
		if (!owner_ && ctx_)
		{
			owner_ = &get_runtime();
			QJSCPP_DEBUG("context track() @" << (void*)this << ": add");
			owner_->contexts_.insert_head(*this);
		}
	}
	
	inline void context::track(context& from)
	{
		if (from.owner_)
		{
			QJSCPP_DEBUG("context track(value&) @" << (void*)this << ": abandon: @" << (void*)&from);
			from.untrack();
			from.owner_ = nullptr;
		}
		if (owner_)
		{
			QJSCPP_DEBUG("context track(value&) @" << (void*)this << ": remove");
			unlink();
			owner_ = nullptr;
		}
		if (ctx_)
		{
			QJSCPP_DEBUG("context track(value&) @" << (void*)this << ": add");
			owner_ = &get_runtime();
			owner_->contexts_.insert_head(*this);
		}
	}
	
	inline void context::untrack()
	{
		if (owner_)
		{
			QJSCPP_DEBUG("context untrack() @" << (void*)this << ": remove");
			unlink();
			owner_ = nullptr;
		}
	}

} // namespace quickjs

#endif
