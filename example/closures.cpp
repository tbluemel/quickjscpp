#include <quickjs.hpp>
#include <iostream>
#include <sstream>

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

int main(int argc, char* argv[])
{
	try
	{
		quickjs::runtime rt;
		quickjs::context ctx = rt.new_context();
		quickjs::value global = ctx.get_global_object();
		
		quickjs::value saved_callback;
		
		global.set_property("print", do_print);
		global.set_property("save_callback",
			[&](const quickjs::args& a) -> quickjs::value
			{
				if (!a[0].is_function())
					return quickjs::value::reference_error(a.get_context(), "not a function");
				
				saved_callback = std::move(a[0]);
				return {};
			});
		global.set_property(std::string("call_a_func"),
			[&](const quickjs::args& a, const std::string& name) -> quickjs::value
			{
				// calls global function by name, and forwards the remaining arguments
				return global.get_property(name)(a.begin() + 1, a.end());
			});
		
		quickjs::value ret = ctx.eval(
			"save_callback(function() {\n"
			"    print('Callback was called');\n"
			"    return 'Passed to me: ' + Array.prototype.slice.call(arguments).join(', ');\n"
			"});\n"
			"print('Callback should be saved');\n"
			"call_a_func('print', 'arg1', 2, 3.45, function() {}, 'arg5', null);\n");
		
		if (saved_callback.valid())
		{
			std::cout << "Calling saved callback: " << saved_callback.as_string() << std::endl;
			quickjs::value ret = saved_callback(1, "arg #2", quickjs::value::null(ctx), true);
			std::cout << "Value returned from callback: " << (ret.valid() ? ret.as_string() : "[invalid]") << std::endl;
		}
		else
			std::cout << "No callback saved!" << std::endl;
	}
	catch (const quickjs::exception& e)
	{
		std::cerr << "quickjs exception: " << e.what() << std::endl;
	}
}
