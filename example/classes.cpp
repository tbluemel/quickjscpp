#include <quickjs.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>

static quickjs::value do_print(const quickjs::args& a)
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
	std::cout << "print: " << oss.str() << std::endl;
	return {};
}

class my_exception
{
	std::string msg_;
	
public:
	my_exception(const std::string& msg):
		msg_(msg)
	{
	}
	
	const std::string& msg() const
	{
		return msg_;
	}
};

class my_class
{
	quickjs::value first_arg_;

public:
	static quickjs::class_def<my_class> class_definition;
	
	my_class(const quickjs::args& a):
		first_arg_(a[0])
	{
		std::cout << "my_class @ " << (void*)this << std::endl;
		std::string val;
		if (a[0].is_string() && a[0].as_string(val))
		{
			std::cout << "my_class arg[0] = " << val << std::endl;
			if (val == "fatal")
			{
				std::cout << "my_class triggers an unrecoverable error" << std::endl;
				// Throwing and exception in the constructor is fine
				throw my_exception("unrecoverable error");
			}
			else if (val == "throw")
			{
				// throw a quickjs::throw_exception to trigger a recoverable exception
				std::cout << "my_class throws an exception" << std::endl;
				throw quickjs::throw_exception(quickjs::value(a.get_context(), "the exception"));
			}
		}
		else
			std::cout << "my_class arg[0] not a string" << std::endl;
	}
	
	~my_class()
	{
		std::cout << "~my_class @ " << (void*)this << std::endl;
	}
	
	quickjs::value get_first_arg(const quickjs::args& /*a*/)
	{
		std::cout << "my_class::get_first_arg @ " << (void*)this << ": returns: "
			<< (first_arg_.valid() ? first_arg_.as_string() : "[invalid]") << std::endl;
		return first_arg_;
	}
	
	quickjs::value fluid_call(const quickjs::args& a)
	{
		std::cout << "my_class::fluid_call @ " << (void*)this << ": return myself" << std::endl;
		return a.get_this();
	}
};

quickjs::class_def<my_class> my_class::class_definition = quickjs::runtime::create_class_def<my_class>("my_class", 1,
	quickjs::object<my_class>::function<&my_class::fluid_call>("fluid_call"),
	quickjs::object<my_class>::function<&my_class::get_first_arg>("get_first_arg"));

static void classes_1()
{
	std::cout << "Example classes_1:" << std::endl;
	try
	{
		quickjs::runtime rt;
		quickjs::context ctx = rt.new_context();
		ctx.register_class<my_class>();
		quickjs::value global = ctx.get_global_object();
		
		global.set_property("print", do_print);
		global.set_property("run_gc",
			[&](const quickjs::args& /*a*/) -> quickjs::value
			{
				// This triggers gc_mark()
				rt.run_gc();
				return {};
			});
		global.set_property("get_my_class",
			[&](const quickjs::args& a) -> quickjs::value
			{
				my_class* inst;
				auto ret = a.get_context().make_object<my_class>(a, inst);
				std::cout << "created instance: @ " << (void*)inst << std::endl;
				return ret;
			});
		
		quickjs::value ret = ctx.eval(
			"var c1 = get_my_class('created by get_my_class()');\n"
			"print('value of first argument:', c1.fluid_call().get_first_arg());\n"
			"var c2 = new my_class('created using new my_class()');\n"
			"print(c1, c2, 'equal:', c1 == c2);"
			"['success', 'throw', 'fatal'].forEach(function(arg) {\n"
			"    try {\n"
			"        var c = get_my_class(arg);\n"
			"        print('created class:', c);\n"
			"        run_gc();\n"
			"    } catch (ex) {\n"
			"        print('Caught exception:', ex);\n"
			"    }\n"
			"});\n");
	}
	catch (const my_exception& e)
	{
		std::cerr << "my_exception: " << e.msg() << std::endl;
	}
	catch (const quickjs::exception& e)
	{
		std::cerr << "quickjs exception: " << e.what() << std::endl;
	}
}

class my_class_shared
{
	quickjs::value first_arg_;
	quickjs::value written_val_;

public:
	static quickjs::class_def_shared<my_class_shared> class_definition;
	
	my_class_shared()
	{
		std::cout << "my_class_shared @ " << (void*)this << " created outside of context" << std::endl;
	}
	
	my_class_shared(const quickjs::args& a):
		first_arg_(a[0])
	{
		std::cout << "my_class_shared @ " << (void*)this << std::endl;
	}
	
	~my_class_shared()
	{
		std::cout << "~my_class_shared @ " << (void*)this << std::endl;
	}
	
	quickjs::value fluid_call(const quickjs::args& a)
	{
		std::cout << "my_class_shared::fluid_call @ " << (void*)this << ": return myself" << std::endl;
		return a.get_this();
	}
	
	quickjs::value readonly_property(const quickjs::value& thisObj)
	{
		return quickjs::value(thisObj.get_context(), "this is a read-only property value");
	}
	
	void writeonly_property(const quickjs::value& thisObj, const quickjs::value& val)
	{
		std::string str;
		if (val.as_string(str))
		{
			if (str == "fatal")
			{
				std::cout << "my_class triggers an unrecoverable error" << std::endl;
				// This throws an unrecoverable error
				throw my_exception("unrecoverable error");
			}
			else if (str == "throw")
			{
				// throw a quickjs::throw_exception to trigger a recoverable exception
				std::cout << "my_class throws an exception" << std::endl;
				throw quickjs::throw_exception(quickjs::value(thisObj.get_context(), "the exception"));
			}
		}
		
		written_val_ = std::move(val);
	}
	
	quickjs::value last_written_val_property(const quickjs::value& /*thisObj*/)
	{
		return written_val_;
	}
	
	quickjs::value getter_a_property(const quickjs::value& thisObj)
	{
		throw quickjs::throw_exception(quickjs::value(thisObj.get_context(), "reading from a_property not implemented"));
	}
	
	void setter_a_property(const quickjs::value& thisObj, const quickjs::value& /*val*/)
	{
		throw quickjs::throw_exception(quickjs::value(thisObj.get_context(), "writing to a_property not implemented"));
	}
	
	void gc_mark(quickjs::value::mark_func mark)
	{
		std::cout << "my_class_shared::gc_mark @ " << (void*)this << std::endl;
		// Mark all raw JSValue this class instance might care about.
		// This is not typically needed, as all quickjs::value objects
		// are automatically accounted for.
	}
	
	bool check_valid() const
	{
		return first_arg_.valid();
	}
};

quickjs::class_def_shared<my_class_shared> my_class_shared::class_definition = quickjs::runtime::create_class_def_shared<my_class_shared>("my_class_shared", 1,
	quickjs::object<my_class_shared>::function<&my_class_shared::fluid_call>("fluid_call"),
	quickjs::object<my_class_shared>::getset<&my_class_shared::getter_a_property, &my_class_shared::setter_a_property>("a_property"),
	quickjs::object<my_class_shared>::get_only<&my_class_shared::readonly_property>("readonly_property"),
	quickjs::object<my_class_shared>::set_only<&my_class_shared::writeonly_property>("writeonly_property"),
	quickjs::object<my_class_shared>::get_only<&my_class_shared::last_written_val_property>("last_written_val_property"));

static void classes_2()
{
	std::cout << "Example classes_2:" << std::endl;
	std::shared_ptr<my_class_shared> last_created_instance;
	std::shared_ptr<my_class_shared> some_other_instance = std::make_shared<my_class_shared>();
	try
	{
		quickjs::runtime rt;
		
		{
			quickjs::context ctx = rt.new_context();
			ctx.register_class<my_class_shared>();
			quickjs::value global = ctx.get_global_object();
			
			global.set_property("print", do_print);
			global.set_property("run_gc",
				[&](const quickjs::args& /*a*/) -> quickjs::value
				{
					// This triggers gc_mark()
					rt.run_gc();
					return {};
				});
			global.set_property("get_my_class_shared",
				[&](const quickjs::args& a) -> quickjs::value
				{
					std::shared_ptr<my_class_shared> inst;
					auto ret = a.get_context().make_object<my_class_shared>(a, inst);
					std::cout << "created instance: @ " << (void*)inst.get() << std::endl;
					last_created_instance = std::move(inst);
					return ret;
				});
			global.set_property("get_other_instance",
				[&](const quickjs::args& a) -> quickjs::value
				{
					// Return an object to an already created instance
					// NOTE that this intentionally only works with shared_ptr instances!
					// Internally, weak references are maintained for object equality purposes
					return quickjs::value(a.get_context(), some_other_instance);
				});
			
			ctx.eval(
				"var o1 = get_other_instance();\n"
				"print('o1 readonly_property =', o1.readonly_property);\n"
				"var o2 = get_other_instance();\n"
				"print('o1.valueOf =', o1.valueOf(),'o2.valueOf =', o2.valueOf());\n"
				"print('o1 should be equal with o2', o1 == o2, o1 === o2);\n"
				"var o3 = o1;\n"
				"print('o3 should be equal to o1:', o1 == o3, o1 === o3);\n"
				"var c1 = get_my_class_shared('created by get_my_class_shared()');\n"
				"var c2 = new my_class_shared('created using new my_class_shared()');\n"
				"try {\n"
				"    print('c1.readonly_property = ' + c1.fluid_call().readonly_property);\n"
				"    c1.readonly_property = 'new value';\n"
				"} catch (ex) {\n"
				"    print('Writing to readonly_property failed:', ex);\n"
				"}\n"
				"try {\n"
				"    var val = c1.writeonly_property;\n"
				"} catch (ex) {\n"
				"    print('Reading from write_only_property failed:', ex);\n"
				"}\n"
				"try {\n"
				"    var val = c1.a_property;\n"
				"} catch (ex) {\n"
				"    print('read from a_property failed:', ex);\n"
				"}\n"
				"try {\n"
				"    c1.a_property = 'some value';\n"
				"} catch (ex) {\n"
				"    print('write to a_property failed:', ex);\n"
				"}\n"
				"try {\n"
				"    print('Value written to writeonly_property (before):', c1.last_written_val_property);\n"
				"    c1.writeonly_property = 'value written';\n"
				"    print('Value written to writeonly_property (after):', c1.last_written_val_property);\n"
				"    c1.writeonly_property = 'throw';\n"
				"} catch (ex) {\n"
				"    print('writeonly_property triggered exception:', ex);\n"
				"}\n");
			
			if (last_created_instance)
				std::cout << "Instance value is valid: " << (last_created_instance->check_valid() ? "yes" : "no") << std::endl;
			
			ctx.eval("try {\n"
				"    print('triggering unrecoverable error...');\n"
				"    c1.writeonly_property = 'fatal';\n"
				"} catch (ex) {\n"
				"    print('This should not ever print!');\n"
				"}\n");
			
			// Let the ctx get out of scope, which invalidates all variables associated with it, and destroys it
		}
		
		if (last_created_instance)
		{
			// The context was destroyed, we still have an instance but the value it's holding onto should be invalid now
			std::cout << "Context is gone, instance value is valid: " << (last_created_instance->check_valid() ? "yes" : "no") << std::endl;
		}
	}
	catch (const my_exception& e)
	{
		std::cerr << "my_exception: " << e.msg() << std::endl;
	}
	catch (const quickjs::exception& e)
	{
		std::cerr << "quickjs exception: " << e.what() << std::endl;
	}
	
	if (last_created_instance)
	{
		// The entire runtime was destroyed, we still have an instance but the value it's holding onto should still be invalid
		std::cout << "Runtime is gone, instance value is valid: " << (last_created_instance->check_valid() ? "yes" : "no") << std::endl;
	}
	else
		std::cout << "get_my_class_shared() was never called!" << std::endl;
}

int main(int argc, char* argv[])
{
	classes_1();
	classes_2();
	return 0;
}

