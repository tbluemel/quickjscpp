# quickjscpp

quickjscpp is a header-only wrapper around the [quickjs](https://bellard.org/quickjs/) JavaScript engine, which allows easy integration into C++11 code.

This wrapper also automatically tracks the lifetime of values and objects, is exception-safe, and automates clean-up.

Supported [quickjs](https://bellard.org/quickjs/) version: [2021-03-27](https://bellard.org/quickjs/quickjs-2021-03-27.tar.xz) (plus patches)

You will need to apply the [patches](patches) for features not (yet) present in the upstream [quickjs](https://bellard.org/quickjs/) project.

# Example
```cpp
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
```

More examples can be found in the [example](example) folder.

# Features

## Managed lifetime

The `quickjs::runtime`, `quickjs::context`, and `quickjs::value` classes manage lifetime for you. `quickjs::value` objects are like weak references: If the `quickjs::context` goes out of scope, they become invalid automatically. If the `quickjs::runtime` goes out of scope, all `quickjs::context` and `quickjs::value` objects become invalid.

## Exception safety

You can throw C++ exceptions and they will traverse through the QuickJS stack in a safe manner, even through several levels. No leaked objects, references, or memory.

## Object lifetime

Objects can be instantiated either "raw" or "shared". A raw object's life time is tied to the context(s), and is deleted when the last reference is dropped. A "shared" object is maintained with a `std::shared_ptr`, which can outlast the `quickjs::context` or `quickjs::runtime`. They can even be created directly from the application, and brought into a `quickjs::context` as a `quickjs::value` at any given time.

Along with this, object equality for classes is guaranteed. By maintaining a weak reference for classes, a new `quickjs::value` can be created and passed to the JS code, and comparison with any other reference to that same object will yield `true`.

The QuickJS library checks for leaked objects, this library takes care of cleaning them up automatically.

## Threads

The same requirements in regards to multi-threading as for the QuickJS library apply to this library. It is not designed to be used by multiple threads!

# TODO

* Modules
* Nicer syntax and expansion of member function arguments (requires c++17)

# Installation

This is a header-only library, simply include the quickjs.hpp file and use it. You still need to link against the QuickJS library that has the [required patches](patches) applied.

# License
quickjscpp is licensed under [MIT](https://opensource.org/licenses/MIT).
