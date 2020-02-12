#include "quickjs.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <set>
#include <memory>

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
};

int main(int argc, char* argv[])
{
	try
	{
		boost::asio::io_context io;
		
		quickjs::runtime rt;
		quickjs::context ctx = rt.new_context();
		
		quickjs::value global = ctx.get_global_object();
		global.set_property("print", do_print);
		
		std::set<std::shared_ptr<boost::asio::steady_timer>> timers;
		global.set_property("setTimeout",
			[&](const quickjs::value callback, int32_t ms) -> void
			{
				if (!callback.is_function())
					throw quickjs::throw_exception(quickjs::value::type_error(callback.get_context(), "not a function"));
				if (ms < 0)
					throw quickjs::throw_exception(quickjs::value::type_error(callback.get_context(), "invalid interval"));
				
				auto timer = std::make_shared<boost::asio::steady_timer>(io);
				timer->expires_from_now(std::chrono::milliseconds(ms));
				timers.insert(timer);
				timer->async_wait(
					[&, timer, callback](const boost::system::error_code& err)
					{
						timers.erase(timer);
						if (!err)
							callback();
					});
			});
		
		boost::asio::steady_timer total_timeout(io);
		total_timeout.expires_from_now(std::chrono::seconds(5));
		total_timeout.async_wait(
			[&](const boost::system::error_code& err)
			{
				if (!err)
				{
					std::cout << "abort main loop" << std::endl;
					io.stop();
				}
			});
		
		quickjs::value ret = ctx.eval(
			"function main() {\n"
			"    var args = arguments;\n"
			"    setTimeout(function() {\n"
			"        print('Handling timer, have ' + args.length + ' args');\n"
			"        for (var i = 0; i < args.length; i++) {\n"
			"            var arg = args[i];\n"
			"            if (arg instanceof Function) {\n"
			"                print('arg[' + i + '] =', arg());\n"
			"                arg('throw_my_exception');\n"
			"            } else\n"
			"                print('arg[' + i + '] =', arg);\n"
			"        }\n"
			"        print('Handling timer complete');\n"
			"    }, 1000);\n"
			"    return 'main function set up a timer';\n"
			"}\n"
			"print('script loaded');\n"
		);
		if (ret.is_exception())
			throw quickjs::exception("exception: " + ret.as_string());
		
		quickjs::value mainFunc = global.get_property("main");
		io.post(
			[mainFunc]()
			{
				// main(2, "three", "four", null, undefined, 56.78, function() { [closure] });
				quickjs::value ret = mainFunc(2, "three", std::string("four"), quickjs::value::null(mainFunc.get_context()), quickjs::value::undefined(mainFunc.get_context()), 56.78,
					[&](const quickjs::args& a) -> quickjs::value
					{
						std::cout << "main function called closure" << std::endl;
						if (a.size() > 0 && a[0].is_string())
						{
							std::string val;
							if (a[0].as_string(val) && val == "throw_my_exception")
							{
								std::cout << "throwing my_exception" << std::endl;
								throw my_exception(); // Yes, that's safe to throw exceptions!
							}
						}
						return quickjs::value(a.get_context(), "some return value");
					}, "looks like my_exception wasn't thrown???");
				std::cout << "Calling main() returned: " << (ret.valid() ? ret.as_cstring() : "[invalid]") << std::endl;
			});
		
		std::cout << "main loop running" << std::endl;
		io.run();
		
		std::cout << "main loop complete" << std::endl;
	}
	catch (const quickjs::value_error& e)
	{
		std::cout << "quickjs error: " << e.what() << std::endl
			<< "Stack trace: " << e.stack() << std::endl;
	}
	catch (const quickjs::value_exception& e)
	{
		std::cout << "quickjs exception thrown: " << e.what() << std::endl;
	}
	catch (const quickjs::exception& e)
	{
		std::cout << "unhandled quickjs exception: " << e.what() << std::endl;
	}
	catch (const my_exception&)
	{
		std::cout << "caught my_exception" << std::endl;
	}
	
	return 0;
}
