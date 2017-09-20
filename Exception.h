// using standard exceptions
#include <iostream>
#include <exception>

//using namespace std;

class PluginExceptiom : public std::exception
{
	virtual const char* what() const throw()
	{
		return "My exception happened";
	}
};


/*
int main()
{
	myexception myex;

	try
	{
		throw myex;
	}
	catch (exception& e)
	{
		cout << e.what() << endl;
	}
	return 0;
}*/