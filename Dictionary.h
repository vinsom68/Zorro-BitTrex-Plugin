#ifndef PFZ_COLLECTIONS_DICTIONARY_H
#define PFZ_COLLECTIONS_DICTIONARY_H

#include "ObjectPool.h"
#include <assert.h>

template<typename T>
class DefaultEqualityComparer
{
};


#define IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(T) \
template<> \
class DefaultEqualityComparer<T> \
{ \
public: \
	static size_t GetHashCode(T value) \
	{ \
		return (size_t)value; \
	} \
	static bool Equals(T value1, T value2) \
	{ \
		return value1 == value2; \
	} \
}

IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(signed char);
IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(unsigned char);
IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(signed short);
IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(unsigned short);
IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(signed int);
IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(unsigned int);
IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(signed long int);
IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(unsigned long int);

// I am really considering that if untyped pointers are used as a key, then there's
// nothing inside the "objects" that will generate the hash. It is the pointer
// itself that should be considered as the hash.
IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(void *);

// The long long int aren't implemented by default as this can cause bad hashing in 32-bit
// computers. Instead of trying to give defaults I prefer that users implement it
// the way they choose.
// IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(signed long long int);
// IMPLEMENT_DEFAULT_EQUALITY_COMPARER_AS_DIRECT_COMPARISON_AND_CAST(unsigned long long int);

template<typename T>
class IEnumerator
{
public:
	virtual ~IEnumerator(){}
	virtual const T *GetNext() = 0;
};

template<typename T>
class ICountAwareEnumerator:
	public IEnumerator<T>
{
public:
	virtual size_t GetCount() const = 0;
};

template<typename TKey, typename TValue>
class Pair
{
private:
	TKey _key;
	TValue _value;

public:
	Pair(const TKey &key, const TValue &value):
		_key(key),
		_value(value)
	{
	}

	const TKey &GetKey() const
	{
		return _key;
	}
	const TValue &GetValue() const
	{
		return _value;
	}
};

template<typename TKey, typename TValue, class TEqualityComparer=DefaultEqualityComparer<TKey>, class TMemoryAllocator=DefaultMemoryAllocator>
class Dictionary
{
private:
	struct _Node
	{
		_Node *_nextNode;
		size_t _hashCode;
		Pair<TKey, TValue> _pair;

		_Node(_Node *nextNode, size_t hashCode, const TKey &key, const TValue &value):
			_nextNode(nextNode),
			_hashCode(hashCode),
			_pair(key, value)
		{
		}
	};

	static inline bool _IsEmpty(_Node *node)
	{
		return node->_nextNode == _GetEmptyPointer();
	}
	static inline _Node *_GetEmptyPointer()
	{
		return (_Node *)(((char *)0)-1);
	}

	// We try to use only prime numbers as the capacities (number of buckets).
	// Yet, for performance reasons, we don't really look for a real prime, only
	// a number that's not divisible from the primes up to 31.
	static size_t _AdaptSize(size_t size)
	{
		if (size <= 31)
			return 31;

		if (size % 2 == 0)
			size --;
		else
			size -= 2;

		while(true)
		{
			size += 2;

			if (size % 3 == 0) continue;
			if (size % 5 == 0) continue;
			if (size % 7 == 0) continue;
			if (size % 11 == 0) continue;
			if (size % 13 == 0) continue;
			if (size % 17 == 0) continue;
			if (size % 19 == 0) continue;
			if (size % 23 == 0) continue;
			if (size % 29 == 0) continue;
			if (size % 31 == 0) continue;

			return size;
		}
	}

	_Node *_buckets;
	size_t _count;
	size_t _capacity;
	ObjectPool<_Node, TMemoryAllocator> *_pool;

	ObjectPool<_Node, TMemoryAllocator> *_GetPool()
	{
		if (_pool == NULL)
			_pool = new ObjectPool<_Node, TMemoryAllocator>();

		return _pool;
	}
	void _Resize()
	{
		size_t newCapacity = _AdaptSize(_capacity * 2);
		if (newCapacity < _capacity)
			throw std::out_of_range("The new required capacity is not supported in this environment.");

		_ResizeAlreadyAdaptedSize(newCapacity);
	}

	void _ResizeAlreadyAdaptedSize(size_t newCapacity)
	{
		ObjectPool<_Node, TMemoryAllocator> *newPool = NULL;
		_Node *newBuckets = (_Node *)TMemoryAllocator::Allocate(newCapacity * sizeof(_Node));
		if (newBuckets == NULL)
			throw std::bad_alloc();

		for(size_t i=0; i<newCapacity; i++)
			newBuckets[i]._nextNode = _GetEmptyPointer();

		if (_count > 0)
		{
			try
			{
				size_t newCount = 0;

				for(size_t i=0; i<_capacity; i++)
				{
					_Node *oldNode = &_buckets[i];
					if (_IsEmpty(oldNode))
						continue;

					do
					{
						size_t hashCode = oldNode->_hashCode;
						size_t newBucketIndex = hashCode % newCapacity;
						_Node *newFirstNode = &newBuckets[newBucketIndex];

						if (_IsEmpty(newFirstNode))
						{
							new (newFirstNode) _Node(NULL, hashCode, oldNode->_pair.GetKey(), oldNode->_pair.GetValue());
						}
						else
						{
							if (newPool == NULL)
								newPool = new ObjectPool<_Node, TMemoryAllocator>();

							_Node *newNode = newPool->GetNextWithoutInitializing();
							new (newNode) _Node(newFirstNode->_nextNode, hashCode, oldNode->_pair.GetKey(), oldNode->_pair.GetValue());
							newFirstNode->_nextNode = newNode;
						}

						oldNode = oldNode->_nextNode;

						newCount++;

#ifndef _DEBUG
						if (newCount == _count)
							goto exitWhileAndFor;
#endif

					} while (oldNode);
				}

				assert(newCount == _count);

#ifndef _DEBUG
				exitWhileAndFor:
				{
					// block needed to avoid compilation errors.
				}
#endif
			}
			catch(...)
			{
				// if there's an exception we clean up
				// our new allocated objects, without touching
				// the previous ones. As long as the possible
				// assignment operators don't touch the
				// old values, the dictionary should not get
				// corrupted and no leaks should happen.
				// If the copy constructors did move content from one
				// place to another, then the dictionary will be corrupted,
				// without memory leaks. So, the caller will be responsible
				// for dealing with the corrupted data or killing the dictionary.
				for(size_t i=0; i<newCapacity; i++)
				{
					_Node *node = &newBuckets[i];

					if (!_IsEmpty(node))
					{
						do
						{
							node->~_Node();
							node = node->_nextNode;
						} while(node);
					}
				}

				delete newPool;

				throw;
			}
		}

		_Node *oldBuckets = _buckets;
		ObjectPool<_Node, TMemoryAllocator> *oldPool = _pool;
		size_t oldCapacity = _capacity;

		_capacity = newCapacity;
		_buckets = newBuckets;
		_pool = newPool;

		// destroy all inner nodes and then the oldbuckets and pool.
		for(size_t i=0; i<oldCapacity; i++)
		{
			_Node *node = &oldBuckets[i];

			if (!_IsEmpty(node))
			{
				do
				{
					node->~_Node();
					node = node->_nextNode;
				} while(node);
			}
		}

		TMemoryAllocator::Deallocate(oldBuckets, oldCapacity * sizeof(_Node));
		delete oldPool;
	}

	Dictionary(const Dictionary<TKey, TValue, TMemoryAllocator> &source);
	void operator = (const Dictionary<TKey, TValue, TMemoryAllocator> &source);

public:
	explicit Dictionary(size_t capacity=31):
		_buckets(NULL),
		_count(0),
		_capacity(_AdaptSize(capacity)),
		_pool(NULL)
	{
		_buckets = (_Node *)TMemoryAllocator::Allocate(_capacity * sizeof(_Node));
		if (_buckets == NULL)
			throw std::bad_alloc();

		for(size_t i=0; i<_capacity; i++)
			_buckets[i]._nextNode = _GetEmptyPointer();
	}
	~Dictionary()
	{
		if (_count == 0)
			goto deallocate;

		int clearCount = 0;
		for(size_t i=0; i<_capacity; i++)
		{
			_Node *node = &_buckets[i];

			if (!_IsEmpty(node))
			{
				do
				{
					node->~_Node();
					node = node->_nextNode;

					clearCount ++;

					if (clearCount == _count)
					{
						assert(node == NULL);
						goto deallocate;
					}
				} while(node);
			}
		}

		assert(clearCount == _count);
		
		deallocate:
		TMemoryAllocator::Deallocate(_buckets, _capacity * sizeof(_Node));
		delete _pool;
	}

	// Gets the number of associations done by this dictionary.
	size_t GetCount() const
	{
		return _count;
	}

	// Gets the number of association slots already allocated, independently
	// on how many associations are currently in use.
	size_t GetCapacity() const
	{
		return _capacity;
	}

	// Tries to set the capacity (number of "association slots") allocated to the given
	// value. If such value is less than Count an exception is thrown. This function returns
	// false if the capacity is the same as the actual one (or if it becomes the same by the
	// prime search logic). It returns true if the capacity was correctly changed. Note
	// that the final capacity may be bigger than the one you asked for.
	bool SetCapacity(size_t newCapacity)
	{
		if (newCapacity < _count)
			throw std::out_of_range("newCapacity must at least equals count.");

		size_t originalParameter = newCapacity;
		newCapacity = _AdaptSize(newCapacity);
		if (newCapacity < originalParameter)
			throw std::out_of_range("The new capacity is not supported in this environment.");

		if (newCapacity == _capacity)
			return false;

		_ResizeAlreadyAdaptedSize(newCapacity);
		return true;
	}

	// Tries to reduce the capacity to the same size as the number of items in
	// this dictionary.
	bool TrimExcess()
	{
		return SetCapacity(_count);
	}

	// Returns a value indicating if the given key exists in this dictionary.
	bool ContainsKey(const TKey &key) const
	{
		return TryGetValue(key) != NULL;
	}

	// Tries to get the value for a given key.
	// Teh return is either the address of the Value or NULL.
	TValue *TryGetValue(const TKey &key) const
	{
		size_t hashCode = TEqualityComparer::GetHashCode(key);
		size_t bucketIndex = hashCode % _capacity;
		_Node *firstNode = &_buckets[bucketIndex];

		if (_IsEmpty(firstNode))
			return NULL;

		_Node *node = firstNode;
		do
		{
			if (hashCode == node->_hashCode)
				if (TEqualityComparer::Equals(key, node->_pair.GetKey()))
					return const_cast<TValue *>(&node->_pair.GetValue());

			node = node->_nextNode;
		} while (node);

		return NULL;
	}

	// Gets the value for a given key.
	// Throws an exception if there's no value for such a key.
	TValue &GetValue(const TKey &key) const
	{
		TValue *result = TryGetValue(key);

		if (result == NULL)
			throw std::invalid_argument("There's no value for the given key.");

		return *result;
	}

	// Gets the value associated with the given key.
	// If there's no value associated with the given key, the default TValue()
	// is returned.
	TValue GetValueOrDefault(const TKey &key) const
	{
		TValue *result = TryGetValue(key);

		if (result == NULL)
			return TValue();

		return *result;
	}

	// Gets the value associated with the given key.
	// If there's no value associated with the given key, the provided
	// defaultValue is returned.
	TValue GetValueOrDefault(const TKey &key, const TValue &defaultValue) const
	{
		TValue *result = TryGetValue(key);

		if (result == NULL)
			return defaultValue;

		return *result;
	}

	// Adds a key/value pair to this dictionary.
	void Add(const TKey &key, const TValue &value)
	{
		if (!TryAdd(key, value))
			throw std::invalid_argument("There's already a value for the given key.");
	}

	// Tries to add a key/value pair to this dictionary.
	// If the pair is added, the return is true.
	// If there's an already existing association, nothing is changed
	// and the function returns false.
	bool TryAdd(const TKey &key, const TValue &value)
	{
		size_t hashCode = TEqualityComparer::GetHashCode(key);
		size_t bucketIndex = hashCode % _capacity;
		_Node *firstNode = &_buckets[bucketIndex];

		if (_IsEmpty(firstNode))
		{
			new (firstNode) _Node(NULL, hashCode, key, value);
			_count++;
			return true;
		}

		_Node *node = firstNode;
		do
		{
			if (hashCode == node->_hashCode)
				if (TEqualityComparer::Equals(key, node->_pair.GetKey()))
					return false;

			node = node->_nextNode;
		} while (node);

		if (_count >= _capacity)
		{
			_Resize();
			bucketIndex = hashCode % _capacity;
			firstNode = &_buckets[bucketIndex];

			if (_IsEmpty(firstNode))
			{
				new (firstNode) _Node(NULL, hashCode, key, value);
				_count++;
				return true;
			}
		}

		node = _GetPool()->GetNextWithoutInitializing();
		new (node) _Node(firstNode->_nextNode, hashCode, key, value);
		firstNode->_nextNode = node;
		_count++;
		return true;
	}

	// Sets a value for the given key, replacing a previous association
	// or adding a new one if necessary.
	void Set(const TKey &key, const TValue &value)
	{
		size_t hashCode = TEqualityComparer::GetHashCode(key);
		size_t bucketIndex = hashCode % _capacity;
		_Node *firstNode = &_buckets[bucketIndex];

		if (_IsEmpty(firstNode))
		{
			new (firstNode) _Node(NULL, hashCode, key, value);
			_count++;
			return;
		}

		_Node *node = firstNode;
		do
		{
			if (hashCode == node->_hashCode)
			{
				if (TEqualityComparer::Equals(key, node->_pair.GetKey()))
				{
					const_cast<TValue &>(node->_pair.GetValue()) = value;
					return;
				}
			}

			node = node->_nextNode;
		} while (node);

		if (_count >= _capacity)
		{
			_Resize();
			bucketIndex = hashCode % _capacity;
			firstNode = &_buckets[bucketIndex];

			if (_IsEmpty(firstNode))
			{
				new (firstNode) _Node(NULL, hashCode, key, value);
				_count++;
				return;
			}
		}

		node = _GetPool()->GetNextWithoutInitializing();
		new (node) _Node(firstNode->_nextNode, hashCode, key, value);
		firstNode->_nextNode = node;
		_count++;
	}

	// Gets the value for a given key or creates one using the given
	// value creator.
	template<class TValueCreator>
	TValue &GetOrCreateValue(const TKey &key, const TValueCreator &valueCreator)
	{
		size_t hashCode = TEqualityComparer::GetHashCode(key);
		size_t bucketIndex = hashCode % _capacity;
		_Node *firstNode = &_buckets[bucketIndex];

		if (_IsEmpty(firstNode))
		{
			new (firstNode) _Node(NULL, hashCode, key, valueCreator(key));
			_count++;
			return const_cast<TValue &>(firstNode->_pair.GetValue());
		}

		_Node *node = firstNode;
		do
		{
			if (hashCode == node->_hashCode)
				if (TEqualityComparer::Equals(key, node->_pair.GetKey()))
					return const_cast<TValue &>(node->_pair.GetValue());

			node = node->_nextNode;
		} while (node);

		if (_count >= _capacity)
		{
			_Resize();
			bucketIndex = hashCode % _capacity;
			firstNode = &_buckets[bucketIndex];

			if (_IsEmpty(firstNode))
			{
				new (firstNode) _Node(NULL, hashCode, key, valueCreator(key));
				_count++;
				return const_cast<TValue &>(firstNode->_pair.GetValue());
			}
		}

		node = _GetPool()->GetNextWithoutInitializing();
		new (node) _Node(firstNode->_nextNode, hashCode, key, valueCreator(key));
		firstNode->_nextNode = node;
		_count++;
		return const_cast<TValue &>(node->_pair.GetValue());
	}

	// Gets the value for a given key or creates one using the given
	// value creator.
	template<typename TContextData>
	TValue &GetOrCreateValue(const TKey &key, TValue (*valueCreator)(const TKey &key, TContextData *contextData), TContextData *contextData)
	{
		size_t hashCode = TEqualityComparer::GetHashCode(key);
		size_t bucketIndex = hashCode % _capacity;
		_Node *firstNode = &_buckets[bucketIndex];

		if (_IsEmpty(firstNode))
		{
			new (firstNode) _Node(NULL, hashCode, key, valueCreator(key, contextData));
			_count++;
			return const_cast<TValue &>(firstNode->_pair.GetValue());
		}

		_Node *node = firstNode;
		do
		{
			if (hashCode == node->_hashCode)
				if (TEqualityComparer::Equals(key, node->_pair.GetKey()))
					return const_cast<TValue &>(node->_pair.GetValue());

			node = node->_nextNode;
		} while (node);

		if (_count >= _capacity)
		{
			_Resize();
			bucketIndex = hashCode % _capacity;
			firstNode = &_buckets[bucketIndex];

			if (_IsEmpty(firstNode))
			{
				new (firstNode) _Node(NULL, hashCode, key, valueCreator(key, contextData));
				_count++;
				return const_cast<TValue &>(firstNode->_pair.GetValue());
			}
		}

		node = _GetPool()->GetNextWithoutInitializing();
		new (node) _Node(firstNode->_nextNode, hashCode, key, valueCreator(key, contextData));
		firstNode->_nextNode = node;
		_count++;
		return const_cast<TValue &>(node->_pair.GetValue());
	}

	// Removes all items in this dictionary.
	// Note that the Capacity is not reset by this action, so if you really want
	// to reduce the memory utilisation, do a TrimExcess or SetCapacity after this call.
	void Clear()
	{
		if (_count == 0)
			return;

		int clearCount = 0;
		for(size_t i=0; i<_capacity; i++)
		{
			_Node *node = &_buckets[i];

			if (!_IsEmpty(node))
			{
				bool isFirst = true;
				do
				{
					_Node *nextNode = node->_nextNode;
					node->~_Node();

					if (isFirst)
					{
						node->_nextNode = _GetEmptyPointer();
						isFirst = false;
					}
					else
					{
						assert(_pool);
						_pool->DeleteWithoutDestroying(node);
					}

					clearCount ++;

					if (clearCount == _count)
					{
						assert(nextNode == NULL);

						_count = 0;
						return;
					}

					node = nextNode;
				} while(node);
			}
		}

		assert(clearCount == _count);
		_count = 0;
	}

	// Removes an association of a key/value pair. The search is done by the key only
	// and the return is true if such key was found (and removed) or false if it was
	// not found (so, nothing changed in the dictionary).
	bool Remove(const TKey &key)
	{
		size_t hashCode = TEqualityComparer::GetHashCode(key);
		size_t bucketIndex = hashCode % _capacity;
		_Node *firstNode = &_buckets[bucketIndex];

		if (_IsEmpty(firstNode))
			return false;

		_Node *previousNode = NULL;
		_Node *node = firstNode;
		do
		{
			if (hashCode == node->_hashCode)
			{
				if (TEqualityComparer::Equals(key, node->_pair.GetKey()))
				{
					if (node == firstNode)
					{
						assert(previousNode == NULL);
						_Node *nextNode = node->_nextNode;
						node->~_Node();

						if (nextNode == NULL)
							node->_nextNode = _GetEmptyPointer();
						else
						{
							assert(_pool);
							new (node) _Node(nextNode->_nextNode, nextNode->_hashCode, nextNode->_pair.GetKey(), nextNode->_pair.GetValue());
							_pool->Delete(nextNode);
						}
					}
					else
					{
						assert(previousNode != NULL);
						assert(_pool);

						previousNode->_nextNode = node->_nextNode;
						_pool->Delete(node);
					}

					_count--;
					return true;
				}
			}

			previousNode = node;
			node = node->_nextNode;
		} while (node);

		return false;
	}

	class DictionaryEnumerator:
		public ICountAwareEnumerator<Pair<TKey, TValue>>
	{
		Dictionary<TKey, TValue, TEqualityComparer, TMemoryAllocator> *_dictionary;
		size_t _bucketIndex;
		_Node *_node;

	public:
		DictionaryEnumerator(Dictionary<TKey, TValue, TEqualityComparer, TMemoryAllocator> *dictionary):
			_dictionary(dictionary),
			_bucketIndex(0),
			_node(NULL)
		{
			assert(dictionary);
		}

		size_t GetCount() const
		{
			return _dictionary->_count;
		}

		const Pair<TKey, TValue> *GetNext()
		{
			if (_node)
			{
				_node = _node->_nextNode;

				if (_node)
					return &_node->_pair;

				_bucketIndex++;
			}

			while(true)
			{
				if (_bucketIndex >= _dictionary->_capacity)
				{
					_node = NULL;
					return NULL;
				}

				_node = &_dictionary->_buckets[_bucketIndex];
				if (!_IsEmpty(_node))
					return &_node->_pair;

				_bucketIndex++;
			}
		}
	};

	// Creates an object that's capable of enumerating all key/value pairs
	// that exist in this dictionary. It's up to you to delete the created
	// object.
	DictionaryEnumerator *CreateEnumerator()
	{
		return new DictionaryEnumerator(this);
	}

	class KeysEnumerator:
		public ICountAwareEnumerator<TKey>
	{
	private:
		DictionaryEnumerator _enumerator;

	public:
		KeysEnumerator(Dictionary<TKey, TValue, TEqualityComparer, TMemoryAllocator> *dictionary):
			_enumerator(dictionary)
		{
		}

		size_t GetCount() const
		{
			return _enumerator.GetCount();
		}

		const TKey *GetNext()
		{
			const Pair<TKey, TValue> *pair = _enumerator.GetNext();
			if (pair)
				return &pair->GetKey();

			return NULL;
		}
	};

	// Creates an object that's capable of enumerating all keys
	// that exist in this dictionary. It's up to you to delete the created
	// object.
	KeysEnumerator *CreateKeysEnumerator()
	{
		return new KeysEnumerator(this);
	}

	class ValuesEnumerator:
		public ICountAwareEnumerator<TValue>
	{
	private:
		DictionaryEnumerator _enumerator;

	public:
		ValuesEnumerator(Dictionary<TKey, TValue, TEqualityComparer, TMemoryAllocator> *dictionary):
			_enumerator(dictionary)
		{
		}

		size_t GetCount() const
		{
			return _enumerator.GetCount();
		}

		const TValue *GetNext()
		{
			const Pair<TKey, TValue> *pair = _enumerator.GetNext();
			if (pair)
				return &pair->GetValue();

			return NULL;
		}
	};

	// Creates an object that's capable of enumerating all values
	// that exist in this dictionary. It's up to you to delete the created
	// object.
	ValuesEnumerator *CreateValuesEnumerator()
	{
		return new ValuesEnumerator(this);
	}
};

#endif
