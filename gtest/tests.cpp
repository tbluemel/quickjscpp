#include <quickjs.hpp>
#include <gtest/gtest.h>

struct QuickJSCpp:
	public ::testing::Test
{
	quickjs::runtime rt_;
	quickjs::context ctx_;
	quickjs::value g_;
	std::vector<std::string> printed_;
	
	void clear_printed()
	{
		printed_.clear();
	}
	
	template <typename... Args>
	inline void validate_lines(const std::vector<std::string>& lines, Args&&... args)
	{
		size_t nargs = sizeof...(args);
		ASSERT_EQ(nargs, lines.size());
		struct d
		{
			static void compare_line(size_t& i, const std::vector<std::string>& printed, const std::string& expected)
			{
				auto got = printed[i++];
				ASSERT_EQ(expected, got);
			}
		};
		size_t i = 0;
		
		// C++17 has fold expressions...
		__attribute__((unused)) int dummy[] = {{0}, ((void)d::compare_line(i, lines, std::forward<Args>(args)), 0)... };
	}
	
	template <typename... Args>
	inline void validate_printed(Args&&... args)
	{
		validate_lines(printed_, std::forward<Args>(args)...);
	}
	
	void SetUp()
	{
		ctx_ = rt_.new_context();
		g_ = ctx_.get_global_object();
		g_.set_property("print", quickjs::value(ctx_,
			[&](const quickjs::args& a)
			{
				std::ostringstream oss;
				bool first = true;
				for (size_t i = 0; i < a.size(); i++)
				{
					if (first)
						first = false;
					else
						oss << ' ';
					oss << a[i].as_string();
				}
				printed_.push_back(oss.str());
				//std::cerr << "print: " << oss.str() << std::endl;
			}));
	}
	
	template <typename E, typename T, typename V>
	void expect_exception(T test_code, V validate_code)
	{
		auto f =
			[&]()
			{
				try
				{
					clear_printed();
					test_code();
				}
				catch (const E& ex)
				{
					validate_code(ex);
					throw;
				}
			};
		EXPECT_THROW(f(), E);
	}
	
	template <typename T, typename V>
	void expect_value_exception(T test_code, V validate_code)
	{
		expect_exception<quickjs::value_exception>(test_code, validate_code);
	}
};

#define TYPE_ERR_NOT_A_FUNCTION "TypeError: not a function"

TEST_F(QuickJSCpp, CallGlobalFunction)
{
	ASSERT_TRUE(ctx_.eval("function main() {}").valid());
	ASSERT_TRUE(ctx_.call_global("main").valid());
	
	SCOPED_TRACE("non_existing");
	expect_value_exception(
		[&] {
			ctx_.call_global("non_existing");
		}, [](const quickjs::value_exception& ex) {
			ASSERT_EQ(ex.val().as_string(), TYPE_ERR_NOT_A_FUNCTION);
		});
}

TEST_F(QuickJSCpp, ThrowException)
{
	{
		SCOPED_TRACE("ThrowException 1");
		expect_value_exception(
			[&] {
				ctx_.eval("throw 'test value';");
			}, [](const quickjs::value_exception& ex) {
				ASSERT_EQ(ex.val().as_string(), "test value");
			});
	}
	auto f1 = quickjs::value(ctx_,
		[&](const quickjs::args& a)
		{
			throw quickjs::throw_exception(quickjs::value(a.get_context(), "test value2"));
		});
	
	{
		SCOPED_TRACE("ThrowException 2");
		expect_value_exception(
			[&] {
				f1();
			}, [](const quickjs::value_exception& ex) {
				ASSERT_EQ(ex.val().as_string(), "test value2");
			});
	}
	{
		SCOPED_TRACE("ThrowException 3");
		expect_value_exception(
			[&] {
				ASSERT_TRUE(ctx_.eval("function call_arg(f) { return f(); }").is_undefined());
				g_.call_member("call_arg", f1);
			}, [](const quickjs::value_exception& ex) {
				ASSERT_EQ(ex.val().as_string(), "test value2");
			});
	}
}

TEST_F(QuickJSCpp, ThrowExceptionPropagation)
{
	struct t
	{
		static quickjs::value handler(const quickjs::args& a, uint32_t level, const quickjs::value& func, const std::string& action)
		{
			auto& ctx = a.get_context();
			if (level == 0)
			{
				ctx.get_global_object().call_member("print", "action:", action);
				
				if (action == "throw exception")
				{
					throw quickjs::throw_exception(quickjs::value(a.get_context(), "did throw"));
				}
				else if (action == "return exception value")
				{
					return quickjs::value::exception(a.get_context(), "returned exception");
				}
				else
					return a[2]; // stop recursion, return action
			}
			else
				return func(level - 1, quickjs::value(a.get_context(), handler), action);
		}
	};
	
	static const auto js_code =
		"function call_recursive(l, f, a) { \n"
		"    print('call_recursive --->', l); \n"
		"    try { \n"
		"        var ret = f(l, call_recursive, a); \n"
		"        print('<-- call_recursive l:', l, 'return:', ret); \n"
		"        return ret; \n"
		"    } catch (ex) { \n"
		"        print('<-- call_recursive (caught)', l, 'ex:', ex); \n"
		"        throw ex; \n"
		"    } \n"
		"}";
	ASSERT_TRUE(ctx_.eval(js_code).is_undefined());
	
	auto f = quickjs::value(ctx_, t::handler);
	{
		SCOPED_TRACE("ThrowExceptionPropagation throw_exception");
		expect_value_exception(
			[&] {
				g_.call_member("call_recursive", 3, f, "throw exception");
			}, [&](const quickjs::value_exception& ex) {
				ASSERT_EQ(ex.val().as_string(), "did throw");
				validate_printed(
					"call_recursive ---> 3",
					"call_recursive ---> 2",
					"call_recursive ---> 1",
					"call_recursive ---> 0",
					"action: throw exception",
					"<-- call_recursive (caught) 0 ex: did throw",
					"<-- call_recursive (caught) 1 ex: did throw",
					"<-- call_recursive (caught) 2 ex: did throw",
					"<-- call_recursive (caught) 3 ex: did throw"
				);
			});
	}
	
	{
		SCOPED_TRACE("ThrowExceptionPropagation return exception value");
		clear_printed();
		expect_value_exception(
			[&] {
				g_.call_member("call_recursive", 3, f, "return exception value");
			}, [&](const quickjs::value_exception& ex) {
				ASSERT_EQ(ex.val().as_string(), "returned exception");
				validate_printed(
					"call_recursive ---> 3",
					"call_recursive ---> 2",
					"call_recursive ---> 1",
					"call_recursive ---> 0",
					"action: return exception value",
					"<-- call_recursive (caught) 0 ex: returned exception",
					"<-- call_recursive (caught) 1 ex: returned exception",
					"<-- call_recursive (caught) 2 ex: returned exception",
					"<-- call_recursive (caught) 3 ex: returned exception"
				);
			});
	}
	
	{
		SCOPED_TRACE("ThrowExceptionPropagation stop recursion");
		clear_printed();
		ASSERT_EQ(g_.call_member("call_recursive", 3, f, "stop recursion").as_string(), "stop recursion");
		validate_printed(
			"call_recursive ---> 3",
			"call_recursive ---> 2",
			"call_recursive ---> 1",
			"call_recursive ---> 0",
			"action: stop recursion",
			"<-- call_recursive l: 0 return: stop recursion",
			"<-- call_recursive l: 1 return: stop recursion",
			"<-- call_recursive l: 2 return: stop recursion",
			"<-- call_recursive l: 3 return: stop recursion"
		);
	}
}

TEST_F(QuickJSCpp, ArgsCount)
{
	struct called_with_n_args
	{
		const size_t nargs;
		const std::vector<std::string> lines;
		
		called_with_n_args(size_t nargs = 0, std::vector<std::string>&& lines = {}):
			nargs(nargs),
			lines(std::move(lines))
		{
		}
	};
	
	struct t
	{
		static std::string val_with_type(const quickjs::value& val)
		{
			std::ostringstream oss;
			if (val.is_exception())
				oss << "exception: ";
			else if (val.is_null())
				oss << "null: ";
			else if (val.is_undefined())
				oss << "undefined: ";
			else if (val.is_bool())
				oss << "bool: ";
			else if (val.is_number())
				oss << "number: ";
			else if (val.is_string())
				oss << "string: ";
			else if (val.is_object())
				oss << "object: ";
			else if (val.is_function())
				oss << "function: ";
			else
				oss << "[unknown]: ";
			if (auto cstr = val.as_cstring())
				oss << cstr.c_str();
			return oss.str();
		}
		
		static quickjs::value func_a(const quickjs::args& a)
		{
			std::vector<std::string> l;
			for (size_t i = 0; i < a.size(); i++)
				l.push_back(val_with_type(a[i]));
			throw called_with_n_args(a.size(), std::move(l));
		}
		static void func_b()
		{
			throw called_with_n_args();
		}
		static void func_c(const quickjs::value& arg1)
		{
			throw called_with_n_args(1, { val_with_type(arg1) });
		}
		static void func_d(const quickjs::value& arg1, const quickjs::value& arg2, const std::string& arg3, bool arg4, bool arg5, int arg6, const quickjs::value& arg7)
		{
			throw called_with_n_args(7, { val_with_type(arg1), val_with_type(arg2), arg3, arg4 ? "true" : "false", arg5 ? "true" : "false", std::to_string(arg6), val_with_type(arg7) });
		}
	};
	
	SCOPED_TRACE("ArgsCount func_a (1)");
	expect_exception<called_with_n_args>(
		[&]()
		{
			quickjs::value(ctx_, t::func_a)();
		},
		[&](const called_with_n_args& ex)
		{
			ASSERT_EQ(ex.nargs, 0);
			ASSERT_TRUE(ex.lines.empty());
		});
	SCOPED_TRACE("ArgsCount func_a (2)");
	expect_exception<called_with_n_args>(
		[&]()
		{
			quickjs::value(ctx_, t::func_a)(1, "arg2", std::string("arg3"), true);
		},
		[&](const called_with_n_args& ex)
		{
			ASSERT_EQ(ex.nargs, 4);
			validate_lines(ex.lines, "number: 1", "string: arg2", "string: arg3", "bool: true");
		});
	SCOPED_TRACE("ArgsCount func_b (1)");
	expect_exception<called_with_n_args>(
		[&]()
		{
			quickjs::value(ctx_, t::func_b)();
		},
		[&](const called_with_n_args& ex)
		{
			ASSERT_EQ(ex.nargs, 0);
			ASSERT_TRUE(ex.lines.empty());
		});
	SCOPED_TRACE("ArgsCount func_b (2)");
	expect_exception<called_with_n_args>(
		[&]()
		{
			quickjs::value(ctx_, t::func_b)(1, "arg2", std::string("arg3"), true);
		},
		[&](const called_with_n_args& ex)
		{
			ASSERT_EQ(ex.nargs, 0);
			ASSERT_TRUE(ex.lines.empty());
		});
	SCOPED_TRACE("ArgsCount func_c (1)");
	expect_exception<called_with_n_args>(
		[&]()
		{
			quickjs::value(ctx_, t::func_c)();
		},
		[&](const called_with_n_args& ex)
		{
			ASSERT_EQ(ex.nargs, 1);
			validate_lines(ex.lines, "undefined: undefined");
		});
	SCOPED_TRACE("ArgsCount func_c (2)");
	expect_exception<called_with_n_args>(
		[&]()
		{
			quickjs::value(ctx_, t::func_c)(45.678, std::string("arg2"));
		},
		[&](const called_with_n_args& ex)
		{
			ASSERT_EQ(ex.nargs, 1);
			validate_lines(ex.lines, "number: 45.678");
		});
	SCOPED_TRACE("ArgsCount func_d (1)");
	expect_exception<called_with_n_args>(
		[&]()
		{
			quickjs::value(ctx_, t::func_d)();
		},
		[&](const called_with_n_args& ex)
		{
			ASSERT_EQ(ex.nargs, 7);
			validate_lines(ex.lines, "undefined: undefined", "undefined: undefined", "", "false", "false", "0", "undefined: undefined");
		});
	SCOPED_TRACE("ArgsCount func_d (2)");
	expect_exception<called_with_n_args>(
		[&]()
		{
			quickjs::value(ctx_, t::func_d)(1, "arg2");
		},
		[&](const called_with_n_args& ex)
		{
			ASSERT_EQ(ex.nargs, 7);
			validate_lines(ex.lines, "number: 1", "string: arg2", "", "false", "false", "0", "undefined: undefined");
		});
	SCOPED_TRACE("ArgsCount func_d (3)");
	expect_exception<called_with_n_args>(
		[&]()
		{
			quickjs::value(ctx_, t::func_d)(1, "arg2", std::string("arg3"), false, true, std::string("arg5"), "arg6", 123, true);
		},
		[&](const called_with_n_args& ex)
		{
			ASSERT_EQ(ex.nargs, 7);
			validate_lines(ex.lines, "number: 1", "string: arg2", "arg3", "false", "true", "0", "string: arg6");
		});
}
