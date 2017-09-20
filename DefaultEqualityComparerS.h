#pragma once
//class DefaultEqualityComparerS
//{
//public:
//
//	DefaultEqualityComparerS()
//	{
//	}
//
//	~DefaultEqualityComparerS()
//	{
//	}
//};

#include <tchar.h>
#include <iostream>
#include <fstream>
#include "Dictionary.h"

template<>
class DefaultEqualityComparer<std::string>
{
public:
	static bool Equals(const std::string &value1, const std::string &value2)
	{
		return value1 == value2;
	}

	static size_t GetHashCode(const std::string &value)
	{
		size_t length = value.length();
		if (length < sizeof(size_t))
		{
			if (length == 0)
				return 0;

			const char *cString = value.c_str();
			size_t result = 0;
			for (size_t i = 0; i<length; i++)
			{
				result <<= 8;
				result |= *cString;
				cString++;
			}

			return result;
		}

		const char *cString = value.c_str();
		size_t *asSizeTPointer = (size_t *)cString;
		size_t result = *asSizeTPointer;

		size_t lastCharactersToUseCount = length - sizeof(size_t);
		if (lastCharactersToUseCount > sizeof(size_t))
			lastCharactersToUseCount = sizeof(size_t);

		if (lastCharactersToUseCount > 0)
		{
			size_t otherResult;

			if (lastCharactersToUseCount == sizeof(size_t))
				otherResult = *((size_t *)(cString + length - sizeof(size_t)));
			else
			{
				otherResult = 0;
				cString += length;
				for (size_t i = 0; i<lastCharactersToUseCount; i++)
				{
					cString--;
					otherResult <<= 8;
					otherResult |= *cString;
				}
			}

			result ^= otherResult;
			result ^= length;
		}

		return result;
	}
};
