#include <quickjs.hpp>
#include <iostream>

int main(int argc, char* argv[])
{
	try
	{
		quickjs::runtime rt;
		quickjs::context ctx = rt.new_context();
		
		quickjs::value global = ctx.get_global_object();
		global.set_property("test_func",
			[&](const std::string& val)
			{
				std::cout << "test_func: " << val << std::endl;
				return val;
			});
		global.set_property("test_func2",
			[&](const quickjs::args& a)
			{
				std::cout << "test_func2 with " << a.size() << " arg(s):" << std::endl;
				for (size_t i = 0; i < a.size(); i++)
					std::cout << "    [" << i << "]: " << a[i].as_string() << std::endl;
			});
		
		quickjs::value ret = ctx.eval(
			"test_func2(test_func('Hello world!'), 3, 4.5);\n"
			"'done'");
		std::cout << "Value returned: " << (ret.valid() ? ret.as_cstring() : "[invalid]") << std::endl;
		return 0;
	}
	catch (const quickjs::exception& e)
	{
		std::cerr << "quickjs exception: " << e.what() << std::endl;
		return 1;
	}
}
