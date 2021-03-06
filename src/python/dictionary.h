#pragma once
#include "../npycrf/common.h"

namespace npycrf {
	namespace python {
		class Dictionary{
		public:
			hashmap<wchar_t, int> _map_character_to_id;	// すべての文字
			hashmap<int, wchar_t> _map_id_to_character;	// すべての文字
			Dictionary(){
				_map_character_to_id[SPECIAL_CHARACTER_UNK] = 0;	// <unk>
				_map_character_to_id[SPECIAL_CHARACTER_BEGIN] = 1;	// <s>
				_map_character_to_id[SPECIAL_CHARACTER_END] = 2;	// </s>
			}
			Dictionary(std::string filename);
			int add_character(wchar_t character);
			int get_character_id(wchar_t character);
			int get_num_characters();
			bool load(std::string filename);
			bool save(std::string filename);
		};
	}
}