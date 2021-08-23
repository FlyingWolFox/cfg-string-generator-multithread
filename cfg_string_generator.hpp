#ifndef CFG_STRING_GENERATOR_H
#define CFG_STRING_GENERATOR_H

#include "barrier.hpp"
#include "BlockingCollection/BlockingCollection.h"

#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>

#include <string>
#include <vector>
#include <list>
#include <unordered_set>
#include <unordered_map>

#define NUM_OF_THREADS 8

namespace cfg_string_gen
{
	namespace detail {
		// controlled queue algorithm's worker thread
		template <bool low_mem, typename T, typename OutContainer, typename QueueContainer, typename Types>
		void worker_cq(code_machina::BlockingCollection<T, QueueContainer>& queue,
						Barrier& go,
						Barrier& wait,
						const bool& exit,
						const typename Types::string_type& nonterminals,
						const typename Types::auto_t::rules_type& rules,
						code_machina::BlockingCollection<T>& done_queue)
		{
			while (!queue.is_completed()) {
				T s; 
				auto status = queue.take(s);
				if (status != code_machina::BlockingCollectionStatus::Ok) {
					continue;
				}
				// found a null, all strings of this depth were proccessed
				else if (Types::functions::size(s) > 0 && Types::functions::at(s, 0) == '\0') {
					// tell main thread that we are finished
					wait.Wait();
					// wait until main thread tells us to go
					go.Wait();
					if (exit) break;
					continue;
				}
				OutContainer new_strings;
		
				// find the first nonterminal
				typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
				if (pos == Types::string_type::npos) {  // no nonterminal found, string done
					done_queue.add(std::move(s));
					continue;
				}
				// do all derivations possible
				for (auto& substitution: rules.at(Types::functions::at(s, pos))) {
					new_strings.insert(new_strings.end(), Types::functions::template derivate<low_mem>(s, pos, substitution));
				}
				typename Types::size_type added;
				queue.add_bulk(std::make_move_iterator(new_strings.begin()), std::make_move_iterator(new_strings.end()), added);
			}
		}
		
		// inserts done strings on the OutContainer safely
		template <typename T, typename Container>
		void done_guard(code_machina::BlockingCollection<T>& done_queue, Container& done_strings)
		{
			while (!done_queue.is_completed()) {
				T s;
				auto status = done_queue.take(s);
				if (status != code_machina::BlockingCollectionStatus::Ok) {
					continue;
				}
				done_strings.insert(done_strings.end(), std::move(s));
			}
		}
		
		// controlled queue string generator
		// the queue is controlled by the main thread and it controls when the threads should stop
		template<bool derivation, bool low_mem, typename T, typename OutContainer, typename QueueContainer, typename Types>
		OutContainer gen_controlled_queue(const typename Types::auto_t::rules_type& rules, typename Types::size_type depth)
		{
			if (depth == 0)
				return OutContainer();
			typename Types::string_type nonterminals;
			nonterminals.reserve(rules.size());
			for (auto& c: rules) {
				nonterminals.push_back(c.first);
			}
		
			code_machina::BlockingCollection<T, QueueContainer> queue;
			code_machina::BlockingCollection<T> done_queue(100);
			// initial string
			queue.add(Types::functions::new_string("S", std::bool_constant<derivation>{}));
			OutContainer done_strings;
		
			std::thread workers[NUM_OF_THREADS];
			Barrier go(NUM_OF_THREADS + 1);
			Barrier wait(NUM_OF_THREADS + 1);
			bool exit = false;
			// theses "nulls" informs the thread to stop and wait
			std::array<T, NUM_OF_THREADS> nulls; 
			// generate a string with a null character but with a size > 0
			for (typename Types::size_type i = 0; i < NUM_OF_THREADS; i++) {
				nulls[i] = Types::functions::new_string(std::move(std::to_string(i) + std::to_string(i)), std::bool_constant<derivation>{});
				Types::functions::at(nulls[i], 0) = '\0';
			}
			typename Types::size_type dummy = NUM_OF_THREADS;
			queue.add_bulk(nulls.begin(), nulls.end(), dummy);
			for (typename Types::size_type i = 0; i < NUM_OF_THREADS; i++) {
				workers[i] = std::thread(worker_cq<low_mem, T, OutContainer, QueueContainer, Types>, std::ref(queue),
										std::ref(go), std::ref(wait),
										std::ref(exit),
										std::ref(nonterminals), std::ref(rules),
										std::ref(done_queue));
			}
			std::thread done_guard_thread(done_guard<T, OutContainer>,
								std::ref(done_queue), std::ref(done_strings));
		
			wait.Wait();
			depth--;
			for (;depth > 0; depth--) {
				// add nulls and release the threads
				queue.add_bulk(nulls.begin(), nulls.end(), dummy);
				go.Wait();
				wait.Wait();
			}
			// final depth generated, tell threads to exit
			exit = true;
			go.Wait();
			queue.complete_adding();
			// get done strings from the queue
			while (!queue.is_completed()) {
				T s; 
				auto status = queue.take(s);
				if (status != code_machina::BlockingCollectionStatus::Ok) {
					continue;
				}
		
				typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
				if (pos == Types::string_type::npos) {
					done_queue.add(std::move(s));
				}
			}
			done_queue.complete_adding();
			for (typename Types::size_type i = 0; i < NUM_OF_THREADS; i++) {
				workers[i].join();
			}
			done_guard_thread.join();
		
			return done_strings;
		}
		
		// free queue algorithm's worker thread
		template <bool low_mem, typename T, typename Container, typename Types>
		void worker_fq_map(code_machina::BlockingCollection<T, Container>& queue,
						std::atomic_uintmax_t& wait_counter,
						typename Types::size_type depth,
						const typename Types::string_type& nonterminals,
						const typename Types::auto_t::rules_type& rules,
						code_machina::BlockingCollection<T>& done_queue)
		{
			while (!queue.is_completed()) {
				T s; 
				// check if we are stuck with an empty queue.
		
				// NUM_OF_THREADS - 1 (all worker threads except this one) incremented the counter
				// but didn't decrement after queue.take() (yet, possibly)
				if (wait_counter.fetch_add(1, std::memory_order_release) == NUM_OF_THREADS - 1) {
					// the queue is empty, but a thread could just have taken an element
					// from the queue
					if (queue.size() == 0) {
						// NUM_OF_THREADS - 1 (all worker threads except this one) are
						// waiting on the queue lock, they're probably stuck
						if (queue.active_consumers() == 0) {
							// last confirmation, a safe guard (maybe unnecessary)
							if (wait_counter.load(std::memory_order_release) == NUM_OF_THREADS) {
								// all strings were processed
								queue.complete_adding();
								done_queue.complete_adding();
								break;
							}
						}
					}
				}
				auto status = queue.take(s);
				wait_counter.fetch_sub(1, std::memory_order_release);
				if (status != code_machina::BlockingCollectionStatus::Ok) {
					continue;
				}
		
				// optimization: derivate one string and them just copy it
				// possibly prevents copying without new derivation -> adding derivation -> reallocation
				// but this increase copy size. the method from controlled queue also works here.
				// more testing is needed

				typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
				if (pos == Types::string_type::npos) {
					done_queue.add(std::move(s));
					continue;
				}
				const auto& nonterminal = Types::functions::at(s, pos);
				const auto* substitution = &(rules.at(nonterminal).front());
				decltype(s.second) derivations;  //! ugly time saving hack
				for (auto& derivation: s.second) {
					if (derivation.size() >= depth)
						continue;
					derivations.push_back(std::move(derivation));
					if constexpr(low_mem)
						derivations.back().push_back(substitution);
					else
						derivations.back().push_back({pos, substitution});
				}
				if (derivations.size() == 0) continue;
				//? Should we do it?
				//// s.second.clear()
				typename Types::string_type new_string(s.first);
				new_string.replace(pos, 1, *substitution);
				// TODO: can be emplaced
				T t = {new_string, derivations};
				queue.add(t);
		
		
				for (auto it = ++rules.at(nonterminal).begin(); it != rules.at(nonterminal).end(); it++) {
					substitution = &(*it);
					for (auto& derivation: derivations) {
						if constexpr(low_mem)
							derivation.back() = substitution;
						else
							derivation.back() = {pos, substitution};
					}
					typename Types::string_type new_string(s.first, std::false_type{});
					new_string.replace(pos, 1, *substitution);
					T t = {new_string, derivations};
					queue.add(t);
				}
			}
		}
		
		// free queue string gen
		// free queue differs from controlled queue by letting threads manage themselves
		template<bool derivation, bool low_mem, typename T, typename OutContainer, typename QueueContainer, typename Types>
		OutContainer gen_free_queue(const typename Types::auto_t::rules_type& rules, typename Types::size_type depth)
		{
			if (depth == 0)
				return OutContainer();
			typename Types::string_type nonterminals;
			nonterminals.reserve(rules.size());
			for (auto& c: rules) {
				nonterminals.push_back(c.first);
			}
		
			code_machina::BlockingCollection<T, QueueContainer> queue;
			code_machina::BlockingCollection<T> done_queue(100);
			queue.add(Types::functions::new_string("S", std::bool_constant<derivation>{}));
			OutContainer done_strings;
		
			std::thread workers[NUM_OF_THREADS];
			std::atomic_uintmax_t wait_counter = {0};
			for (typename Types::size_type i = 0; i < NUM_OF_THREADS; i++) {
				workers[i] = std::thread(worker_fq_map<low_mem, T, QueueContainer, Types>, std::ref(queue),
										std::ref(wait_counter), depth,
										std::ref(nonterminals), std::ref(rules),
										std::ref(done_queue));
			}
			std::thread done_guard_thread(done_guard<T, OutContainer>,
								std::ref(done_queue), std::ref(done_strings));
		
			for (typename Types::size_type i = 0; i < NUM_OF_THREADS; i++) {
				workers[i].join();
			}
			done_guard_thread.join();
		
			return done_strings;
		}
		
		// dual container algorithm's worker thread
		template <bool derivation, bool low_mem, typename Container, typename Types>
		void worker_dc(const typename Container::iterator& start,
						const typename Container::iterator& end,
						Barrier& go,
						Barrier& wait,
						const bool& exit,
						const typename Types::string_type& nonterminals,
						const typename Types::auto_t::rules_type& rules,
						Container& done_strings,
						Container& new_strings)
		{
			while (true) {
				go.Wait();
				done_strings.clear();
				new_strings.clear();
				if (exit) {
					break;
				}
		
				for (auto it = start; it != end; it++) {
					auto& s = *it;
					typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
					if (pos == Types::string_type::npos) {
						done_strings.insert(done_strings.end(), std::move(s));
						continue;
					}
					for (auto& substitution: rules.at(Types::functions::at(s, pos))) {
						if constexpr(derivation) {
							auto new_string = Types::functions::template derivate<low_mem>(s, pos, substitution);
							auto [it, success] = new_strings.insert(new_string);
							// TODO: support no repetition mode, somehow
							if (!success)
								Types::functions::merge(new_string.second, it->second);
						}
						else {
							new_strings.insert(new_strings.end(), Types::functions::template derivate<low_mem>(s, pos, substitution));
						}
					}
				}
				wait.Wait();
			}
		}
		
		// dual containers string generator
		// this algorithm uses two containers, one with the strings to be derived
		// and one with the newly derivated strings. The first is replaced with the second at the end of derivation
		template<bool derivation, bool low_mem, typename T, typename OutContainer, typename QueueContainer, typename Types>
		OutContainer gen_dual_containers(const typename Types::auto_t::rules_type& rules, typename Types::size_type depth)
		{
			typename Types::string_type nonterminals;
			nonterminals.reserve(rules.size());
			for (auto& c: rules) {
				nonterminals.push_back(c.first);
			}
			OutContainer strings = {Types::functions::new_string("S", std::bool_constant<derivation>{})};
			OutContainer done_strings;
		
			// initial generation. Does it until there's enough strings to feed to threads
			for (;depth > 0 && strings.size() < NUM_OF_THREADS; depth--) {
				OutContainer new_strings;
				for (auto& s: strings) {
					typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
					if (pos == Types::string_type::npos) {
						done_strings.insert(done_strings.end(), std::move(s));
						continue;
					}
					for (auto& substitution: rules.at(Types::functions::at(s, pos))) {
						new_strings.insert(new_strings.end(), Types::functions::template derivate<low_mem>(s, pos, substitution));
					}
				}
				strings = std::move(new_strings);
			}
			// we finshed early
			if (depth == 0) {
				for (auto& s: strings) {
					typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
					if (pos == Types::string_type::npos) {
						done_strings.insert(done_strings.end(), std::move(s));
						continue;
					}
				}
				return done_strings;
			}
		
			std::thread threads[NUM_OF_THREADS];
			typename OutContainer::iterator start[NUM_OF_THREADS];
			typename OutContainer::iterator end[NUM_OF_THREADS];
			Barrier go(NUM_OF_THREADS + 1);
			Barrier wait(NUM_OF_THREADS + 1);
			OutContainer results_done[NUM_OF_THREADS];
			OutContainer results_strings[NUM_OF_THREADS];
			bool exit = false;
			for (typename Types::size_type i = 0; i < NUM_OF_THREADS; i++) {
				threads[i] = std::thread(worker_dc<derivation, low_mem, OutContainer, Types>,
										std::ref(start[i]), std::ref(end[i]), std::ref(go), std::ref(wait),
										std::ref(exit), std::ref(nonterminals), std::ref(rules),
										std::ref(results_done[i]), std::ref(results_strings[i]));
			}
		
			// split the container, getting the start and end iterator of the slices
			// these iterators are given to the threads
			for (;depth > 0; depth--) {
				typename Types::size_type slice = strings.size()/NUM_OF_THREADS;
				start[0] = strings.begin();
				end[0] = std::next(start[0], slice);
				for (typename Types::size_type i = 1; i < NUM_OF_THREADS - 1; i++) {
					start[i] = end[i-1];
					end[i] = std::next(start[i], slice);
				}
				start[NUM_OF_THREADS - 1] = end[NUM_OF_THREADS - 2];
				end[NUM_OF_THREADS - 1] = strings.end();
				go.Wait();
		
				//  extract the generated strings from the threads
				// TODO: possible optimization: clear the strings container and add
				// the new strings directly, so we move strings just once, intead of twice
				// also doable with done_strings, but without the clear call

				OutContainer new_strings;
		
				typename Types::size_type new_done_strings_size = done_strings.size();
				typename Types::size_type new_strings_size = strings.size();
				for (typename Types::size_type i = 0; i < NUM_OF_THREADS; i++) {
					new_done_strings_size += results_done[i].size();
					new_strings_size += results_strings[i].size();
				}
				done_strings.reserve(new_done_strings_size);
				new_strings.reserve(new_strings_size);
		
				wait.Wait();
				for (typename Types::size_type i = 0; i < NUM_OF_THREADS; i++) {
					Types::functions::merge(results_strings[i], new_strings);
					Types::functions::merge(results_done[i], done_strings);
				}
		
				strings = std::move(new_strings);
			}
			// finished, check for done strings in the last batch of generated strings
			exit = true;
			go.Wait();
			for (typename Types::size_type i = 0; i < NUM_OF_THREADS; i++) {
				threads[i].join();
			}
			for (auto& s: strings) {
				typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
				if (pos == Types::string_type::npos) {
					done_strings.insert(done_strings.end(), std::move(s));
				}
			}
			return done_strings;
		}

		// single threaded version of gen_dual_containers
		template<bool derivation, bool low_mem, typename T, typename OutContainer, typename QueueContainer, typename Types>
		OutContainer gen_dual_containers_sth(const typename Types::auto_t::rules_type& rules, typename Types::size_type depth)
		{
			typename Types::string_type nonterminals;
			nonterminals.reserve(rules.size());
			for (auto& c: rules) {
				nonterminals.push_back(c.first);
			}
			OutContainer strings = {Types::functions::new_string("S", std::bool_constant<derivation>{})};
			OutContainer done_strings;
		
			for (;depth > 0; depth--) {
				OutContainer new_strings;
				for (auto& s: strings) {
					typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
					if (pos == Types::string_type::npos) {
						done_strings.insert(done_strings.end(), std::move(s));
						continue;
					}
					for (auto& substitution: rules.at(Types::functions::at(s, pos))) {
						new_strings.insert(new_strings.end(), Types::functions::template derivate<low_mem>(s, pos, substitution));
					}
				}
				strings = std::move(new_strings);
			}
			for (auto& s: strings) {
				typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
				if (pos == Types::string_type::npos) {
					done_strings.insert(done_strings.end(), std::move(s));
					continue;
				}
			}
			return done_strings;
		}

		// single threaded version of gen_controlled_queue
		template<bool derivation, bool low_mem, typename T, typename OutContainer, typename QueueContainer, typename Types>
		OutContainer gen_controlled_queue_sth(const typename Types::auto_t::rules_type& rules, typename Types::size_type depth)
		{
			if (depth == 0)
				return OutContainer();
			typename Types::string_type nonterminals;
			nonterminals.reserve(rules.size());
			for (auto& c: rules) {
				nonterminals.push_back(c.first);
			}
			// using the queue container directly (no BlockingCollection necessary)
			QueueContainer queue;
			queue.try_add(Types::functions::new_string("S", std::bool_constant<derivation>{}));
			OutContainer done_strings;

			auto null = Types::functions::new_string("00", std::bool_constant<derivation>{});
			Types::functions::at(null, 0) = '\0';
			for (;depth > 0; depth--) {
				queue.try_add(null);
				while (true) {
					T s; 
					queue.try_take(s);
					if (Types::functions::size(s) > 0 && Types::functions::at(s, 0) == '\0') {
						break;
					}
					typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
					if (pos == Types::string_type::npos) {
						done_strings.insert(done_strings.end(), std::move(s));
						continue;
					}
					for (auto& substitution: rules.at(Types::functions::at(s, pos))) {
						queue.try_add(Types::functions::template derivate<low_mem>(s, pos, substitution));
					}

				}
			}
			// get done strings from the last batch
			while (queue.size() != 0) {
				T s; 
				auto status = queue.try_take(s);
				typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
				if (pos == Types::string_type::npos) {
					done_strings.insert(done_strings.end(), std::move(s));
				}
			}

			return done_strings;
		}

		// single threaded version of gen_free_queue
		template<bool b_derivation, bool low_mem, typename T, typename OutContainer, typename QueueContainer, typename Types>
		OutContainer gen_free_queue_sth(const typename Types::auto_t::rules_type& rules, typename Types::size_type depth)
		{
			if (depth == 0)
				return OutContainer();
			typename Types::string_type nonterminals;
			nonterminals.reserve(rules.size());
			for (auto& c: rules) {
				nonterminals.push_back(c.first);
			}
			QueueContainer queue;
			queue.try_add(Types::functions::new_string("S", std::bool_constant<b_derivation>{}));
			OutContainer done_strings;

			while (queue.size() != 0) {
				T s; 
				queue.try_take(s);
				typename Types::size_type pos = Types::functions::find_first_of(s, nonterminals);
				if (pos == Types::string_type::npos) {
					done_strings.insert(done_strings.end(), std::move(s));
					continue;
				}
				const auto& nonterminal = Types::functions::at(s, pos);
				const auto* substitution = &(rules.at(nonterminal).front());
				decltype(s.second) derivations;  //! ugly time saving hack
				for (auto& derivation: s.second) {
					if (derivation.size() >= depth)
						continue;
					derivations.push_back(std::move(derivation));
					if constexpr(low_mem)
						derivations.back().push_back(substitution);
					else
						derivations.back().push_back({pos, substitution});
				}
				if (derivations.size() == 0) continue;
				//? Should we do it?
				//// s.second.clear()

				typename Types::string_type new_string(s.first);
				new_string.replace(pos, 1, *substitution);
				T t = {new_string, derivations};
				queue.try_add(t);
		
		
				for (auto it = ++rules.at(nonterminal).begin(); it != rules.at(nonterminal).end(); it++) {
					substitution = &(*it);
					for (auto& derivation: derivations) {
						if constexpr(low_mem)
							derivation.back() = substitution;
						else
							derivation.back() = {pos, substitution};
					}
					typename Types::string_type new_string(s.first, std::false_type{});
					new_string.replace(pos, 1, *substitution);
					T t = {new_string, derivations};
					queue.try_add(t);
				}
			}

			return done_strings;
		}
		
		// setup types, for use on the Types struct, because I
		// couldn't find a way to use template aliases (with using)
		template <typename string_type, typename derivation_type, template <typename T> typename sequence_container,
		template <typename Key> typename set_container, template <typename Key, typename Value> typename map_container,
		typename additive_map_functors>
		struct SetUpTypes {
			using derivations_type = sequence_container<derivation_type>;

			using rules_type = map_container<typename string_type::value_type, sequence_container<string_type>>;

			using repetition_string_container = sequence_container<string_type>;
			using no_rep_string_container = set_container<string_type>;
			using derivation_container = map_container<string_type, sequence_container<derivations_type>>;
			
			using additive_queue = code_machina::AdditiveMapQueueContainer<string_type, sequence_container<derivations_type>,
			typename additive_map_functors::copy, typename additive_map_functors::move, map_container>;
			using conservative_queue = code_machina::ConservativeMapQueueContainer<string_type, sequence_container<derivations_type>, map_container>;
			using queue = code_machina::QueueContainer<string_type>;
			using set_queue = code_machina::SetQueueContainer<string_type, set_container>;
		};

	}

	// the default types to be used on string generation
	template <bool low_memory>
	struct TypeDefs
	{
		template <typename T>
		using sequence_container = std::vector<T>;
		template <typename T>
		using set_container = std::unordered_set<T>;
		template <typename Key, typename Value>
		using map_container = std::unordered_map<Key, Value>;
		template <typename _T1, typename _T2>
		using pair = std::pair<_T1, _T2>;
		using size_type = std::size_t;

		using string_type = std::string;
		using derivation_type = std::conditional_t<low_memory, const string_type*, pair<size_type, const string_type*>>; 

		using derivations_type = sequence_container<derivation_type>;
		//using string_derivation_type = typename map_container<string_type, sequence_container<derivations_type>>::value_type;
		using string_derivation_type = pair<string_type, sequence_container<derivations_type>>;

		// TODO: use this instead of define
		const static size_type num_of_threads = 8;

		// these functions are used to be called generically
		// most mimics string member functions
		// functions with std::true_type are called when working with derivations
		// string-derivation types doesn't need the replace() function
		struct functions {
			inline static string_type new_string(std::string&& str, std::false_type) { return {str}; }
			inline static size_type find_first_of(const string_type& this_str, const string_type& str)
			{
				return this_str.find_first_of(str);
			}
			inline static char& at(string_type& this_str, const size_type pos) { return this_str[pos]; }
			inline static const char& at(const string_type& this_str, const size_type pos) { return this_str[pos]; }
			inline static size_type size(const string_type& this_str) { return this_str.size(); }
			inline static string_type& replace(string_type& this_str, const std::size_t pos, const std::size_t count, const string_type& str)
			{
				return this_str.replace(pos, count, str);
			}
			template <bool low_mem>
			inline static string_type derivate(const string_type& str, size_type pos, const string_type& substitution)
			{ 
				string_type new_string(str);
				replace(new_string, pos, 1, substitution);
				return new_string;
			}

			inline static string_derivation_type new_string(std::string&& str, std::true_type) { return {str, {{}}}; }
			inline static size_type find_first_of(const string_derivation_type& this_str, const string_type& str)
			{
				return find_first_of(this_str.first, str);
			}
			inline static char& at(string_derivation_type& this_str, const size_type pos) { return at(this_str.first, pos); }
			inline static const char& at(const string_derivation_type& this_str, const size_type pos) { return at(this_str.first, pos); }
			inline static size_type size(const string_derivation_type& this_str) { return size(this_str.first); }
			template <bool low_mem>
			inline static string_derivation_type derivate(const string_derivation_type& str, size_type pos, const string_type& substitution)
			{ 
				auto derivations = str.second;
				for (auto& derivation: derivations) {
					if constexpr(low_mem)
						derivation.push_back(&substitution);
					else
						derivation.push_back({pos, &substitution});
				}
				// drops const
				string_type new_string(str.first);
				new_string.replace(pos, 1, substitution);
				return {new_string, derivations};
			}

			template <typename T>
			inline static void merge(sequence_container<T>& src, sequence_container<T>& dest)
			{
				std::move(src.begin(), src.end(), std::back_inserter(dest));
			}

			template <typename AssociativeContainer>
			inline static void merge(AssociativeContainer& src, AssociativeContainer& dest)
			{
				dest.merge(src);
			}
		};


		// used with AdditiveMapQueueContainer
		struct additive_map_functors {
			struct copy {
				void operator()(sequence_container<derivations_type>& value, const sequence_container<derivations_type>& new_value)
				{ 
					std::copy(new_value.begin(), new_value.end(), std::back_inserter(value));
				}
			};
			struct move {
				void operator()(sequence_container<derivations_type>& value, sequence_container<derivations_type>&& new_value)
				{
					std::move(new_value.begin(), new_value.end(), std::back_inserter(value));
				}
			};
		};

		// setup types
		using auto_t = detail::SetUpTypes<string_type, derivation_type, sequence_container, set_container, map_container, additive_map_functors>;
	};

	// main function, generates strings based on rules until depth is reached
	// derivation: enables storing the derivation steps with the string
	// repetition: if duplicate string (ambiguous grammar) should be stored as well
	// for just string generation, this means that there won't bve any duplicates
	// for string and derivation generation, this means that each string will just have one derivation, even if there are multiple
	// low_memory: just affects if derivation is true. If true, the derivations with the string won't have de position of the nonterminal replaced, just the replacement (removal of redundant information)
	// fast: if true, use dual_container intead of controlled queue
	// derivation_fq: just affects if derivation is true. If true, use free_queue instead of controlled_queue
	// TODO: derivation_fq is for testing purposes. Use the fastest implementation for the derivations
	// single_threaded: disbales multithreading if true
	// TypeDefs: struct with the types to be used
	template <bool derivation = false, bool repetition = false, bool low_memory = false, bool fast = false, bool derivation_fq = false, bool single_threaded = false, template <bool low_mem> typename TypeDefs = TypeDefs>
	auto cfg_string_generator(const typename TypeDefs<low_memory>::auto_t::rules_type& rules, typename TypeDefs<low_memory>::size_type depth)
	{
		using Types = TypeDefs<low_memory>;
		if constexpr(derivation) {
			using Container = typename Types::auto_t::derivation_container;
			using QueueContainer = std::conditional_t<repetition, typename Types::auto_t::additive_queue, typename Types::auto_t::conservative_queue>;
			if constexpr(single_threaded) {
				if constexpr(fast)
					return detail::gen_dual_containers_sth<derivation, low_memory, typename Types::string_derivation_type, Container, QueueContainer, Types>(rules, depth);
				else {
					if constexpr(derivation_fq)
						return detail::gen_free_queue_sth<derivation, low_memory, typename Types::string_derivation_type, Container, QueueContainer, Types>(rules, depth);
					else 
						return detail::gen_controlled_queue_sth<derivation, low_memory, typename Types::string_derivation_type, Container, QueueContainer, Types>(rules, depth);

				}
			}
			else {
				if constexpr(fast)
					return detail::gen_dual_containers<derivation, low_memory, typename Types::string_derivation_type, Container, QueueContainer, Types>(rules, depth);
				else {
					if constexpr(derivation_fq)
						return detail::gen_free_queue<derivation, low_memory, typename Types::string_derivation_type, Container, QueueContainer, Types>(rules, depth);
					else 
						return detail::gen_controlled_queue<derivation, low_memory, typename Types::string_derivation_type, Container, QueueContainer, Types>(rules, depth);

				}
			}
		}
		else {
			using Container = std::conditional_t<repetition, typename Types::auto_t::repetition_string_container, typename Types::auto_t::no_rep_string_container>;
			using QueueContainer = std::conditional_t<repetition, typename Types::auto_t::queue, typename Types::auto_t::set_queue>;
			if constexpr(single_threaded) {
				if constexpr(fast)
					return detail::gen_dual_containers_sth<derivation, low_memory, typename Types::string_type, Container, QueueContainer, Types>(rules, depth);
				else
					return detail::gen_controlled_queue_sth<derivation, low_memory, typename Types::string_type, Container, QueueContainer, Types>(rules, depth);
			}
			else {
				if constexpr(fast)
					return detail::gen_dual_containers<derivation, low_memory, typename Types::string_type, Container, QueueContainer, Types>(rules, depth);
				else
					return detail::gen_controlled_queue<derivation, low_memory, typename Types::string_type, Container, QueueContainer, Types>(rules, depth);
			}
		}
	} 
}
#endif // CFG_STRING_GENERATOR_H
