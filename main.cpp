
#include "mimalloc.h"

#include <vector>
#include <string>
#include <string_view>
#include <iostream>
#include <cstdint>
#include <unordered_map>

#include "claujson.h"

namespace clau {

	enum class OrderType : uint64_t { NONE = 0, 
		ADD, // todo.. number? binary operation?
		RETURN, EXIT, 
		VARIABLE, 
		IF, WHILE, COND_END, EQ, NOTEQ,
		STRING, INT, UINT, FLOAT, BOOL, NULL_, 
		PRINT, 
		NEW_LOCAL_ARRAY, NEW_LOCAL_OBJECT,
		CD, // ? and dir?
		SIZE };
	
	struct Order {
		union {
			OrderType type;    // OrderType
			uint64_t pos; // pos of data in DataVector.
		};

		Order(OrderType type) : type(type) { }
		Order(uint64_t pos) : pos(pos) { }
	};

	class TapeA {
	private:
		std::vector<Order> m_order_list;
	public:
		void write(Order e) {
			m_order_list.push_back(e);
		}

		const Order& operator[](uint64_t idx) const {
			return m_order_list[idx];
		}

		uint64_t size() const {
			return m_order_list.size();
		}
	};

	class TapeB {
	private:
		std::vector<claujson::_Value> m_data_list;
	public:
		void write(claujson::_Value e) {
			m_data_list.push_back(std::move(e));
		}

		const claujson::_Value& operator[](uint64_t idx) const {
			return m_data_list[idx];
		}
	};

	struct VM_Func {
		TapeA m_order_tape;
		TapeB m_data_tape;
	};

	class Explorer {
	private:
		claujson::_Value* root = nullptr;
		std::vector<std::pair<uint64_t, claujson::Structured*>> _stack;

	public:
		Explorer(claujson::_Value* root) : root(root) {
			if (root->is_primitive()) {
				_stack.push_back({ 0, nullptr });
			}
			else {
				_stack.push_back({ 0, root->as_structured_ptr() });
			}
		}
		Explorer() {
			//
		}

	private:

		claujson::Structured* Now() {
			return _stack.back().second;
		}

		const claujson::Structured* Now() const {
			return _stack.back().second;
		}

	public:
		bool IsPrimitiveRoot() const {
			return root->is_primitive();
		}

		uint64_t GetIdx() const {
			if (_stack.empty()) { return 0; }
			return _stack.back().first;
		}

		void SetIdx(const uint64_t idx) {
			if (_stack.empty()) { return; }
			_stack.back().first = idx;
		}

		claujson::_Value& Get() {
			if (IsPrimitiveRoot()) {
				return *root;
			}
			return Now()->get_value_list(GetIdx());	
		}

		const claujson::_Value& Get() const {
			if (IsPrimitiveRoot()) {
				return *root;
			}
			return Now()->get_value_list(GetIdx());
		}

		const claujson::_Value& GetKey() const {
			static claujson::_Value nkey(nullptr, false);
			if (!Now() || Now()->is_array()) {
				return nkey;
			}
			return Now()->get_const_key_list(GetIdx());
		}

		void ChangeKey(claujson::Value new_key) {
			if (Now()) {
				Now()->change_key(GetKey(), std::move(new_key));
			}
		}

		void Delete() {
			if (Now()) {
				Now()->erase(GetIdx(), true);
			}
		}

		void Enter() {
			if (Get().is_structured()) {
				_stack.push_back({ 0, Get().as_structured_ptr() });
			}
		}

		void Quit() {
			if (_stack.empty()) {
				return;
			}
			_stack.pop_back();
		}

		// Group <- Array or Object!
		bool IsLastElementInGroup() { // END Of GROUP?
			if (IsPrimitiveRoot()) {
				return true;
			}
			if (nullptr == Now()) {
				return true;
			}
			return GetIdx() >= Now()->get_data_size();
		}

		bool Next() {
			if (!IsLastElementInGroup()) {
				SetIdx(GetIdx() + 1);
				return true;
			}
			return false;
		}
		
		// goto using json pointer?, dir is STRING or UNSIGNED_INTEGER
		void Goto(const std::vector<claujson::_Value>& dir) {

		}

		void Dump(std::ofstream& out) {
			while (!IsLastElementInGroup()) {
				if (GetKey().is_str()) {
					out << GetKey() << " : ";
				}
				if (Get().is_primitive()) {
					out << Get() << " ";
				}
				else {
					if (Get().is_array()) {
						out << " [ ";
					}
					else {
						out << " { ";
					}

					Enter();

					Explorer temp = *this;
					temp.Dump(out);

					Quit();

					if (Get().is_array()) {
						out << " ] \n";
					}
					else {
						out << " } \n";
					}
				}
				Next();
			}
		}
	};

	// VM to edit json data.
	class VM {
	private:
		std::unordered_map<std::string, VM_Func> functions;
	private:
		std::vector<claujson::_Value> m_stack; // for calcul? 3+4  ?
		claujson::Value m_root;
		Explorer m_explorer;
	public:
		VM(claujson::Value v) : m_root(std::move(v)) {
			m_explorer = &m_root.Get();
		}
	public:
		// Register VM Function.
		void Register(const std::string& name, TapeA&& order_tape, TapeB&& data_tape) {
			functions.insert(std::make_pair(name, VM_Func{ std::move(order_tape), std::move(data_tape) }));
		}
		void Run(const std::string& start_func_name = "main") {
			const VM_Func& now_func = functions[start_func_name];
			uint64_t program_counter = 0;

			while (program_counter < now_func.m_order_tape.size()) {
				switch (now_func.m_order_tape[program_counter].type) {
				// RETURN
				case OrderType::EXIT:
					program_counter++;
					return;
					break;
				// STRING, INT, UINT, FLOAT, BOOL, NULL_
				case OrderType::INT:
					if (now_func.m_data_tape[now_func.m_order_tape[program_counter + 1].pos].is_int()) {
						program_counter++;
						m_stack.push_back(claujson::_Value(now_func.m_data_tape[now_func.m_order_tape[program_counter].pos].get_integer()));
					}
					else {
						// log, error !
					}
					program_counter++;
					break;
				// + - * / % 
				case OrderType::ADD:
				{
					claujson::_Value x, y, z;
					y = std::move(m_stack.back()); m_stack.pop_back();
					x = std::move(m_stack.back()); m_stack.pop_back();

					if (x.type() == y.type()) {
						switch (x.type()) {
						case claujson::_ValueType::INT:
							z = claujson::_Value(x.get_integer() + y.get_integer());
							break;
						case claujson::_ValueType::UINT:
							z = claujson::_Value(x.get_unsigned_integer() + y.get_unsigned_integer());
							break;
						case claujson::_ValueType::FLOAT:
							z = claujson::_Value(x.get_floating() + y.get_floating());
							break;
						default:
							//
							break;
						}
					}
					m_stack.push_back(std::move(z));
				}
					program_counter++;
					break;
				// PRINT, 
				case OrderType::PRINT:
					std::cout << m_stack.back();
					m_stack.pop_back();
					program_counter++;
					break;
			    // VARIALBLE..
				


				// manipulate Explorer?


				default:
					std::cout << "error";
					break;
				}
			}
		}

		void ExplorerDump(std::ofstream& out) {
			m_explorer.Dump(out);
		}
	};
}


int main(void)
{
	claujson::parser p(16);
	claujson::Document d;
	p.parse("citylots.json", d, 16); // chk exception process..

	clau::VM vm_test(std::move(d.Get()));
	{
		clau::TapeA test;
		test.write(clau::OrderType::INT);
		test.write(0);
		test.write(clau::OrderType::INT);
		test.write(1);
		test.write(clau::OrderType::ADD);
		test.write(clau::OrderType::PRINT);
		test.write(clau::OrderType::EXIT);

		clau::TapeB test2;
		test2.write(claujson::_Value(10));
		test2.write(claujson::_Value(20));
	
		vm_test.Register("main", std::move(test), std::move(test2));
	}

	try {
		vm_test.Run();
		{
			std::ofstream out;
			out.open("save.json", std::ios::binary);
			if (out) {
				vm_test.ExplorerDump(out);
				out.close();
			}
		}
	}
	catch (...) {
		return -1;
	}

	return 0;
}


