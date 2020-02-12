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

static void exceptions_1()
{
	std::cout << "Example exceptions_1:" << std::endl;
	try
	{
		quickjs::runtime rt;
		auto ctx = rt.new_context();
		auto ret = ctx.eval("throw 'my exception'");
		std::cout << "Value returned: " << (ret.valid() ? ret.as_string() : "[invalid]") << std::endl;
	}
	catch (const quickjs::exception& e)
	{
		std::cerr << "quickjs exception: " << e.what() << std::endl;
	}
}

static void exceptions_2()
{
	std::cout << "Example exceptions_2:" << std::endl;
	try
	{
		quickjs::runtime rt;
		auto ctx = rt.new_context();
		auto global = ctx.get_global_object();
		
		global.set_property("print", do_print);
		global.set_property("do_something",
			[](const quickjs::args& a) -> quickjs::value
			{
				// This throws an exception that javascript can catch
				return quickjs::value::reference_error(a.get_context(), "my exception");
			});
		
		auto ret = ctx.eval(
			"try {\n"
			"    print('Calling do_something() which should throw a javascript exception');\n"
			"    do_something();\n"
			"} catch (ex) {\n"
			"    print('Caught exception:', ex);\n"
			"}\n"
			"print('exceptions_2 done');");
		std::cout << "Value returned: " << (ret.valid() ? ret.as_string() : "[invalid]") << std::endl;
	}
	catch (const quickjs::exception& e)
	{
		std::cerr << "quickjs exception: " << e.what() << std::endl;
	}
}

class my_exception
{
};

static void exceptions_3()
{
	std::cout << "Example exceptions_3:" << std::endl;
	try
	{
		quickjs::runtime rt;
		auto ctx = rt.new_context();
		auto global = ctx.get_global_object();
		
		global.set_property("print", do_print);
		global.set_property("do_something",
			[](const quickjs::args& a) -> quickjs::value
			{
				// This throws an exception that javascript can cannot catch
				throw my_exception();
			});
		
		auto ret = ctx.eval(
			"try {\n"
			"    print('Calling do_something() which should abort execution');\n"
			"    do_something();\n"
			"} catch (ex) {\n"
			"    // This should not happen\n"
			"    print('Caught exception:', ex);\n"
			"}\n"
			"print('exceptions_3 done');");
		std::cout << "Value returned: " << (ret.valid() ? ret.as_string() : "[invalid]") << std::endl;
	}
	catch (const quickjs::exception& e)
	{
		std::cerr << "quickjs exception: " << e.what() << std::endl;
	}
	catch (const my_exception&)
	{
		std::cout << "caught my_exception" << std::endl;
	}
}

int main(int argc, char* argv[])
{
	exceptions_1();
	exceptions_2();
	exceptions_3();
	return 0;
}
