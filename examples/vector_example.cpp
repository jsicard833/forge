#include <algorithm>
#include <iostream>
#include <numeric>
#include <string>

#include <forge/vector.hpp>

// Small helper to print a vector's contents and metadata
template <typename T>
void print_vector(const forge::vector<T>& v, const std::string& name = "v")
{
	std::cout << name << ": size=" << v.size() << " capacity=" << v.capacity()
			  << " memory=" << v.memory_usage() << " bytes\n";

	std::cout << "[";
	for (auto it = v.cbegin(); it != v.cend(); ++it)
	{
		if (it != v.cbegin()) std::cout << ", ";
		std::cout << *it;
	}
	std::cout << "]\n";
}

int main()
{
	using namespace forge;

	// Construct from initializer list
	vector<int> v = {1, 2, 3};
	print_vector(v, "initial");

	// push_back and emplace_back
	v.push_back(4);               // copy/move insert
	v.emplace_back(5);            // constructs in-place
	print_vector(v, "after push/emplace");

	// Reserve capacity to avoid reallocations for predictable workloads
	v.reserve(16);
	std::cout << "After reserve(16): capacity=" << v.capacity() << "\n";

	// Insert at arbitrary position
	auto mid = v.begin() + 2; // before the third element
	v.insert(mid, 42);
	print_vector(v, "after insert(42) at index 2");

	// Erase an element (erase returns iterator to the next element)
	v.erase(v.begin()); // remove first element
	print_vector(v, "after erase(begin())");

	// Find / contains
	bool has42 = v.contains(42);
	std::cout << "contains(42): " << (has42 ? "yes" : "no") << "\n";

	auto it = v.find(3);
	if (it != v.end())
		std::cout << "found 3 at index " << (it - v.begin()) << "\n";

	// Use get_view() to obtain a std::span and run algorithms
	auto span = v.get_view(); // non-owning view of elements
	int sum = std::accumulate(span.begin(), span.end(), 0);
	std::cout << "sum(span) = " << sum << "\n";

	// Demonstrate move semantics
	vector<int> moved = std::move(v);
	print_vector(moved, "moved (from v)");
	std::cout << "original v is now: size=" << v.size() << " capacity=" << v.capacity()
			  << "\n";

	// Resize and assign
	moved.resize(8, -1);
	print_vector(moved, "after resize(8, -1)");

	moved.assign({10, 20, 30});
	print_vector(moved, "after assign({10,20,30})");

	// Bounds-checked access example (demonstrates safe API)
	try
	{
		std::cout << "at(5) -> ";
		std::cout << moved.at(5) << "\n"; // will throw for this small vector
	}
	catch (const std::out_of_range& e)
	{
		std::cout << "caught out_of_range: " << e.what() << "\n";
	}

	return 0;
}

