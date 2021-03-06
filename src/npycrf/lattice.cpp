#include <algorithm>
#include <iostream>
#include "hash.h"
#include "sampler.h"
#include "lattice.h"

// ＿人人人人人人人人人人人人人人人人人人人人人人人人人人人人＿
// ＞　Latticeでは文字のインデックスtが1から始まることに注意　＜
// ￣Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y^Y￣

namespace npycrf {
	using namespace npylm;
	Lattice::Lattice(NPYLM* npylm, crf::CRF* crf){
		_npylm = npylm;
		_crf = crf;
		_pure_crf_mode = false;
		_pure_npylm_mode = false;
		_max_sentence_length = 0;
		_max_word_length = 0;
		_word_ids = array<id>(3);
		reserve(npylm->_max_word_length, 1);
	}
	Lattice::~Lattice(){

	}
	// 必要ならキャッシュの再確保
	void Lattice::reserve(int max_word_length, int max_sentence_length){
		if(max_word_length <= _max_word_length && max_sentence_length <= _max_sentence_length){
			return;
		}
		_allocate_capacity(max_word_length, max_sentence_length);
		_max_word_length = max_word_length;
		_max_sentence_length = max_sentence_length;
	}
	void Lattice::_allocate_capacity(int max_word_length, int max_sentence_length){
		_max_word_length = max_word_length;
		_max_sentence_length = max_sentence_length;
		// 必要な配列の初期化
		int seq_capacity = max_sentence_length + 1;
		int word_capacity = max_word_length + 1;
		// 前向き確率のスケーリング係数
		_scaling = array<double>(seq_capacity + 1);
		// ビタビアルゴリズム用
		_viterbi_backward = mat::tri<int>(seq_capacity, word_capacity, word_capacity);
		// 後ろ向きアルゴリズムでkとjをサンプリングするときの確率表
		_backward_sampling_table = array<double>(word_capacity * word_capacity);
		// 前向き確率
		_alpha = mat::tri<double>(seq_capacity + 1, word_capacity, word_capacity);
		// 後向き確率
		_beta = mat::tri<double>(seq_capacity + 1, word_capacity, word_capacity);
		// 部分文字列が単語になる条件付き確率テーブル
		_pc_s = mat::bi<double>(seq_capacity, word_capacity);
		// Markov-CRFの周辺確率テーブル
		_pz_s = mat::tri<double>(seq_capacity + 1, 2, 2);
		// 3-gram確率のキャッシュ
		_pw_h_tkji = mat::quad<double>(seq_capacity + 1, word_capacity, word_capacity, word_capacity);
		// 遷移確率のキャッシュ
		_p_transition_tkji = mat::quad<double>(seq_capacity + 1, word_capacity, word_capacity, word_capacity);
		// 部分単語3-gramの周辺確率テーブル
		_p_conc_tkji = mat::quad<double>(seq_capacity + 1, word_capacity, word_capacity, word_capacity);
		// 部分文字列のIDのキャッシュ
		_substring_word_id_cache = mat::bi<id>(seq_capacity, word_capacity);
	}
	double Lattice::_lambda_0(){
		return _crf->_parameter->_lambda_0;
	}
	void Lattice::set_pure_crf_mode(bool enabled){
		_pure_crf_mode = enabled;
		_pure_npylm_mode = false;
	}
	void Lattice::set_pure_npylm_mode(bool enabled){
		_pure_npylm_mode = enabled;
		_pure_crf_mode = false;
	}
	void Lattice::set_npycrf_mode(){
		_pure_npylm_mode = false;
		_pure_crf_mode = false;
	}
	bool Lattice::get_pure_crf_mode(){
		return _pure_crf_mode;
	}
	id Lattice::get_substring_word_id_at_t_k(Sentence* sentence, int t, int k){
		assert(t <= sentence->size());
		assert(k < _max_sentence_length + 1);
		assert(t - k >= 0);
		if(t == 0){
			return SPECIAL_CHARACTER_BEGIN;
		}
		id word_id = _substring_word_id_cache(t, k);
		if(word_id == 0){
			word_id = sentence->get_substr_word_id(t - k, t - 1);	// 引数はインデックスなので注意
			_substring_word_id_cache(t, k) = word_id;
		}
		return word_id;
	}
	// alpha[t-k][j][i]自体は正規化されている場合があるが、alpha(t, k, j)の正規化はここでは行わない
	// @args
	// 	p_transition_tkji;	NPYLMによる確率計算の結果をpw_h_tkjiにキャッシュする
	// 				このキャッシュは後向き確率の計算時に使う
	void Lattice::_sum_alpha_t_k_j(Sentence* sentence, int t, int k, int j, mat::tri<double> &alpha, mat::quad<double> &pw_h_tkji, mat::quad<double> &p_transition_tkji, double prod_scaling, double crf_potential){
		id word_k_id = get_substring_word_id_at_t_k(sentence, t, k);
		wchar_t const* characters = sentence->_characters;
		array<int> &character_ids = sentence->_character_ids;
		int character_ids_length = sentence->size();
		assert(t <= _max_sentence_length + 1);
		assert(k <= _max_word_length);
		assert(j <= _max_word_length);
		assert(t - k >= 0);
		// <bos>から生成されている場合
		if(j == 0){
			double p_transition = 0;
			if(_pure_crf_mode){
				p_transition = exp(crf_potential);
			}else{
				_word_ids[0] = SPECIAL_CHARACTER_BEGIN;
				_word_ids[1] = SPECIAL_CHARACTER_BEGIN;
				_word_ids[2] = word_k_id;
				double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2, t - k, t - 1);
				assert(pw_h > 0);
				p_transition = (_pure_npylm_mode) ? pw_h : exp(_lambda_0() * log(pw_h) + crf_potential);
				pw_h_tkji(t, k, 0, 0) = pw_h;
				p_transition_tkji(t, k, 0, 0) = p_transition;
			}
			assert(p_transition > 0);
			alpha(t, k, 0) = p_transition * prod_scaling;
			return;
		}
		// i=0に相当
		if(t - k - j == 0){
			double p_transition = 0;
			if(_pure_crf_mode){
				p_transition = exp(crf_potential);
			}else{
				_word_ids[0] = SPECIAL_CHARACTER_BEGIN;
				_word_ids[1] = get_substring_word_id_at_t_k(sentence, t - k, j);
				_word_ids[2] = word_k_id;
				double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2, t - k, t - 1);
				assert(pw_h > 0);
				assert(alpha(t - k, j, 0) > 0);
				p_transition = (_pure_npylm_mode) ? pw_h : exp(_lambda_0() * log(pw_h) + crf_potential);
				pw_h_tkji(t, k, j, 0) = pw_h;
				p_transition_tkji(t, k, j, 0) = p_transition;
			}
			assert(p_transition > 0);
			alpha(t, k, j) = p_transition * alpha(t - k, j, 0) * prod_scaling;
			assert(alpha(t, k, j) > 0);
			return;
		}
		// それ以外の場合は周辺化
		double sum = 0;
		for(int i = 1;i <= std::min(t - k - j, _max_word_length);i++){
			double p_transition = 0;
			if(_pure_crf_mode){
				p_transition = exp(crf_potential);
			}else{
				_word_ids[0] = get_substring_word_id_at_t_k(sentence, t - k - j, i);
				_word_ids[1] = get_substring_word_id_at_t_k(sentence, t - k, j);
				_word_ids[2] = word_k_id;
				double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2, t - k, t - 1);
				assert(pw_h > 0);
				assert(i <= _max_word_length);
				assert(alpha(t - k, j, i) > 0);
				p_transition = (_pure_npylm_mode) ? pw_h : exp(_lambda_0() * log(pw_h) + crf_potential);
				pw_h_tkji(t, k, j, i) = pw_h;
				p_transition_tkji(t, k, j, i) = p_transition;
			}
			assert(p_transition > 0);
			double value = p_transition * alpha(t - k, j, i);

			#ifdef __DEBUG__
				if(value == 0){
					// std::cout << pw_h << std::endl;
					std::cout << alpha(t - k, j, i) << std::endl;
					std::cout << t << ", " << k << ", " << j << ", " << i << std::endl;
				}
			#endif

			assert(value > 0);
			sum += value;
		}
		assert(sum > 0);
		alpha(t, k, j) = sum * prod_scaling;
	}
	void Lattice::forward_filtering(Sentence* sentence, bool use_scaling){
		_enumerate_forward_variables(sentence, _alpha, _pw_h_tkji, _p_transition_tkji, _scaling, use_scaling);
	}
	void Lattice::backward_sampling(Sentence* sentence, std::vector<int> &segments){
		_backward_sampling(sentence, _alpha, _p_transition_tkji, segments);
	}
	// 求めた前向き確率テーブルをもとに後ろから分割をサンプリング
	void Lattice::_backward_sampling(Sentence* sentence, mat::tri<double> &alpha, mat::quad<double> &p_transition_tkji, std::vector<int> &segments){
		segments.clear();
		int k = 0;
		int j = 0;
		int sum = 0;
		int t = sentence->size();
		_sample_backward_k_and_j(sentence, alpha, p_transition_tkji, t, 1, k, j);
		assert(k <= _max_word_length);
		segments.push_back(k);
		if(j == 0 && k == t){	// 文章すべてが1単語になる場合
			return;
		}
		assert(k > 0 && j > 0);
		assert(j <= _max_word_length);
		segments.push_back(j);
		// std::cout << "<- " << k << std::endl;
		// std::cout << "<- " << j << std::endl;
		t -= k;
		t -= j;
		sum += k + j;
		int next_word_length = j;
		while(t > 0){
			if(t == 1){
				k = 1;
				j = 0;
			}else{
				_sample_backward_k_and_j(sentence, alpha, p_transition_tkji, t, next_word_length, k, j);
				assert(k > 0);
				assert(k <= _max_word_length);
			}
			segments.push_back(k);
			// std::cout << "<- " << k << std::endl;
			t -= k;
			if(j == 0){
				assert(t == 0);
			}else{
				assert(j <= _max_word_length);
				segments.push_back(j);
				// std::cout << "<- " << j << std::endl;
				t -= j;
			}
			sum += k + j;
			next_word_length = j;
		}
		assert(t == 0);
		assert(sum == sentence->size());
		reverse(segments.begin(), segments.end());
	}
	// 後向きにkとjをサンプリング
	void Lattice::_sample_backward_k_and_j(Sentence* sentence, mat::tri<double> &alpha, mat::quad<double> &p_transition_tkji, int t, int next_word_length, int &sampled_k, int &sampled_j){
		assert(0 < next_word_length && next_word_length <= _max_word_length);
		assert(_pure_crf_mode == false);
		int table_index = 0;
		wchar_t const* characters = sentence->_characters;
		array<int> &character_ids = sentence->_character_ids;
		int character_ids_length = sentence->size();
		double sum_p = 0;
		int limit_k = std::min(t, _max_word_length);
		for(int k = 1;k <= limit_k;k++){
			for(int j = (t - k == 0) ? 0 : 1;j <= std::min(t - k, _max_word_length);j++){
				double p_transition = p_transition_tkji(t + next_word_length, next_word_length, k, j);
				#ifdef __DEBUG__
					id word_j_id = get_substring_word_id_at_t_k(sentence, t - k, j);
					id word_k_id = get_substring_word_id_at_t_k(sentence, t, k);
					id word_t_id = SPECIAL_CHARACTER_END;
					if(t < sentence->size()){
						assert(t + next_word_length <= sentence->size());
						word_t_id = get_substring_word_id_at_t_k(sentence, t + next_word_length, next_word_length);
						// id word_id = sentence->get_substr_word_id(t - 1, t + next_word_length - 2);
						// assert(word_t_id == word_id);
					}
					_word_ids[0] = word_j_id;
					_word_ids[1] = word_k_id;
					_word_ids[2] = word_t_id;
					double potential = _crf->compute_gamma(sentence, t + 1, t + next_word_length + 1);
					double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2, t, t + next_word_length - 1);
					double _p_transition = exp(_lambda_0() * log(pw_h) + potential);
					assert(p_transition == _p_transition);
				#endif
				assert(alpha(t, k, j) > 0);
				sum_p += p_transition * alpha(t, k, j);
				_backward_sampling_table[table_index] = p_transition * alpha(t, k, j);
				table_index++;
			}
		}
		assert(table_index > 0);
		assert(table_index <= _max_word_length * _max_word_length);
		double normalizer = 1.0 / sum_p;
		double r = sampler::uniform(0, 1);
		int i = 0;
		double stack = 0;
		for(int k = 1;k <= limit_k;k++){
			for(int j = (t - k == 0) ? 0 : 1;j <= std::min(t - k, _max_word_length);j++){
				assert(i < table_index);
				assert(_backward_sampling_table[i] > 0);
				stack += _backward_sampling_table[i] * normalizer;
				if(r <= stack){
					sampled_k = k;
					sampled_j = j;
					return;
				}
				i++;
			}
		}
	}
	// Blocked Gibbs Samplingによる分割のサンプリング
	// 分割結果が確率的に決まる
	// use_scaling=trueでアンダーフローを防ぐ
	void Lattice::blocked_gibbs(Sentence* sentence, std::vector<int> &segments, bool use_scaling){
		assert(sentence->size() <= _max_sentence_length);
		int size = sentence->size() + 1;

		#ifdef __DEBUG__
			for(int t = 0;t < size;t++){
				_scaling[t] = 0;
				for(int k = 0;k < _max_word_length + 1;k++){
					for(int j = 0;j < _max_word_length + 1;j++){
						_alpha(t, k, j) = -1;
					}
				}
			}
			for(int k = 0;k < _max_word_length;k++){
				for(int j = 0;j < _max_word_length;j++){
					_backward_sampling_table[k * _max_word_length + j] = -1;
				}
			}
		#endif

		_alpha(0, 0, 0) = 1;
		_scaling[0] = 1;
		_clear_word_id_cache(sentence->size());
		_clear_p_tkji(sentence->size());
		forward_filtering(sentence, use_scaling);
		backward_sampling(sentence, segments);
	}
	// ビタビアルゴリズム用
	void Lattice::viterbi_argmax_alpha_t_k_j(Sentence* sentence, int t, int k, int j){
		id word_k_id = get_substring_word_id_at_t_k(sentence, t, k);
		wchar_t const* characters = sentence->_characters;
		array<int> &character_ids = sentence->_character_ids;
		int character_ids_length = sentence->size();
		assert(t <= character_ids_length + 1);
		assert(k <= _max_word_length);
		assert(j <= _max_word_length);
		assert(t - k >= 0);
		// <bos>から生成されている場合
		if(j == 0){
			_word_ids[0] = SPECIAL_CHARACTER_BEGIN;
			_word_ids[1] = SPECIAL_CHARACTER_BEGIN;
			_word_ids[2] = word_k_id;
			double log_p_transition = 0;
			double potential = 0;
			if(_pure_crf_mode){
				potential = _crf->compute_gamma(sentence, t - k + 1, t + 1);
				log_p_transition = potential;
			}else{
				if(_pure_npylm_mode == false){
					potential = _crf->compute_gamma(sentence, t - k + 1, t + 1);
				}
				double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2, t - k, t - 1);
				assert(pw_h > 0);
				log_p_transition = (_pure_npylm_mode) ? log(pw_h) : _lambda_0() * log(pw_h) + potential;
			}
			_alpha(t, k, 0) = log_p_transition;
			_viterbi_backward(t, k, 0) = 0;
			return;
		}
		// i=0に相当
		if(t - k - j == 0){
			_word_ids[0] = SPECIAL_CHARACTER_BEGIN;
			_word_ids[1] = get_substring_word_id_at_t_k(sentence, t - k, j);
			_word_ids[2] = word_k_id;
			double log_p_transition = 0;
			double potential = 0;
			if(_pure_crf_mode){
				potential = _crf->compute_gamma(sentence, t - k + 1, t + 1);
				log_p_transition = potential;
			}else{
				if(_pure_npylm_mode == false){
					potential = _crf->compute_gamma(sentence, t - k + 1, t + 1);
				}
				double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2, t - k, t - 1);
				assert(pw_h > 0);
				log_p_transition = (_pure_npylm_mode) ? log(pw_h) : _lambda_0() * log(pw_h) + potential;
			}
			assert(_alpha(t - k, j, 0) != 0);
			_alpha(t, k, j) = log_p_transition + _alpha(t - k, j, 0);
			assert(_alpha(t, k, j) != 0);
			_viterbi_backward(t, k, j) = 0;
			return;
		}
		// それ以外の場合は周辺化
		double max_log_p = 0;
		int argmax = 0;
		for(int i = 1;i <= std::min(t - k - j, _max_word_length);i++){
			_word_ids[0] = get_substring_word_id_at_t_k(sentence, t - k - j, i);
			_word_ids[1] = get_substring_word_id_at_t_k(sentence, t - k, j);
			_word_ids[2] = word_k_id;
			double log_p_transition = 0;
			double potential = 0;
			if(_pure_crf_mode){
				potential = _crf->compute_gamma(sentence, t - k + 1, t + 1);
				log_p_transition = potential;
			}else{
				if(_pure_npylm_mode == false){
					potential = _crf->compute_gamma(sentence, t - k + 1, t + 1);
				}
				double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2, t - k, t - 1);
				assert(pw_h > 0);
				log_p_transition = (_pure_npylm_mode) ? log(pw_h) : _lambda_0() * log(pw_h) + potential;
			}
			assert(i <= _max_word_length);
			assert(_alpha(t - k, j, i) != 0);
			double value = log_p_transition + _alpha(t - k, j, i);
			assert(value != 0);
			if(argmax == 0 || value > max_log_p){
				argmax = i;
				max_log_p = value;
			}
		}
		assert(argmax > 0);
		_alpha(t, k, j) = max_log_p;
		_viterbi_backward(t, k, j) = argmax;
	}
	void Lattice::viterbi_forward(Sentence* sentence){
		for(int t = 1;t <= sentence->size();t++){
			for(int k = 1;k <= std::min(t, _max_word_length);k++){
				if(t - k == 0){
					viterbi_argmax_alpha_t_k_j(sentence, t, k, 0);
				}
				for(int j = 1;j <= std::min(t - k, _max_word_length);j++){
					viterbi_argmax_alpha_t_k_j(sentence, t, k, j);
				}
			}
		}
	}
	// <eos>に繋がる確率でargmax
	void Lattice::viterbi_argmax_backward_k_and_j_to_eos(Sentence* sentence, int t, int &argmax_k, int &argmax_j){
		assert(t == sentence->size());
		wchar_t const* characters = sentence->_characters;
		array<int> &character_ids = sentence->_character_ids;
		int character_ids_length = sentence->size();
		double max_log_p = 0;
		argmax_k = 0;
		argmax_j = 0;
		int limit_k = std::min(t, _max_word_length);
		for(int k = 1;k <= limit_k;k++){
			for(int j = (t - k == 0) ? 0 : 1;j <= std::min(t - k, _max_word_length);j++){
				double log_p_transition = 0;
				if(_pure_crf_mode){
					log_p_transition = _crf->compute_gamma(sentence, t + 1, t + 2);;	// expしない
				}else{
					double potential = 0;
					if(_pure_npylm_mode == false){
						potential = _crf->compute_gamma(sentence, t + 1, t + 2);
					}
					_word_ids[0] = get_substring_word_id_at_t_k(sentence, t - k, j);
					_word_ids[1] = get_substring_word_id_at_t_k(sentence, t, k);
					_word_ids[2] = SPECIAL_CHARACTER_END;
					double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2, t, t);
					assert(pw_h > 0);
					log_p_transition = (_pure_npylm_mode) ? log(pw_h) : _lambda_0() * log(pw_h) + potential;	// expしない
				}
				assert(_alpha(t, k, j) != 0);
				double value = log_p_transition + _alpha(t, k, j);
				assert(value != 0);
				if(argmax_k == 0 || value > max_log_p){
					max_log_p = value;
					argmax_k = k;
					argmax_j = j;
				}
			}
		}
	}
	void Lattice::viterbi_backward(Sentence* sentence, std::vector<int> &segments){
		segments.clear();
		int k = 0;
		int j = 0;
		int sum = 0;
		int t = sentence->size();
		viterbi_argmax_backward_k_and_j_to_eos(sentence, t, k, j);
		assert(k <= _max_word_length);
		segments.push_back(k);
		sum += k;
		if(j == 0 && k == t){	// 文章すべてが1単語になる場合
			assert(sum == sentence->size());
			return;
		}
		assert(k > 0 && j > 0);
		assert(j <= _max_word_length);
		segments.push_back(j);
		int i = _viterbi_backward(t, k, j);
		assert(i >= 0);
		assert(i <= _max_word_length);
		t -= k;
		sum += j;
		sum += i;
		k = j;
		j = i;
		if(i == 0){
			assert(sum == sentence->size());
			return;
		}
		segments.push_back(i);
		while(t > 0){
			i = _viterbi_backward(t, k, j);
			assert(i >= 0);
			assert(i <= _max_word_length);
			if(i != 0){
				segments.push_back(i);
			}
			// std::cout << "<- " << k << std::endl;
			t -= k;
			k = j;
			j = i;
			sum += i;
		}
		assert(t == 0);
		assert(sum == sentence->size());
		assert(segments.size() > 0);
		reverse(segments.begin(), segments.end());
	}
	// ビタビアルゴリズムによる分割
	// 決定的に分割が決まる
	void Lattice::viterbi_decode(Sentence* sentence, std::vector<int> &segments){
		assert(sentence->size() <= _max_sentence_length);
		int size = sentence->size() + 1;

		#ifdef __DEBUG__
			for(int t = 0;t < size;t++){
				for(int k = 0;k < _max_word_length + 1;k++){
					for(int j = 0;j < _max_word_length + 1;j++){
						_alpha(t, k, j) = -1;
					}
				}
				for(int k = 0;k < _max_word_length;k++){
					for(int j = 0;j < _max_word_length;j++){
						_viterbi_backward(t, k, j) = -1;
					}
				}
			}
		#endif

		_alpha(0, 0, 0) = 0;
		_scaling[0] = 1;
		_clear_word_id_cache(sentence->size());
		_clear_p_tkji(sentence->size());
		viterbi_forward(sentence);
		viterbi_backward(sentence, segments);
	}
	// 文の可能な分割全てを考慮した文の確率（<eos>への接続を含む）
	// use_scaling=trueならアンダーフローを防ぐ
	double Lattice::compute_normalizing_constant(Sentence* sentence, bool use_scaling){
		assert(sentence->size() <= _max_sentence_length);
		_clear_word_id_cache(sentence->size());
		_clear_p_tkji(sentence->size());
		// 前向き確率を求める
		_enumerate_forward_variables(sentence, _alpha, _pw_h_tkji, _p_transition_tkji, _scaling, use_scaling);
		// <eos>へ到達する確率を全部足す
		int t = sentence->size() + 1;	// <eos>
		int k = 1;	// <eos>の長さは1
		if(use_scaling){
			// スケーリング係数を使う場合は逆数の積がそのまま文の確率になる
			double px = 1;
			for(int m = 1;m <= t;m++){
				px /= _scaling[m];
			}
			return px;
		}
		double px = 0;
		for(int j = 1;j <= std::min(t - k, _max_word_length);j++){
			px += _alpha(t, k, j);
		}
		return px;
	}
	// 可能な分割全てを考慮した文の確率（<eos>への接続を含む）
	// スケーリング係数は前向き時のみ計算可能なので注意
	double Lattice::_compute_normalizing_constant_backward(Sentence* sentence, mat::tri<double> &beta, mat::quad<double> &p_transition_tkji){
		assert(sentence->size() <= _max_sentence_length);
		_clear_word_id_cache(sentence->size());
		// 後向き確率を求める
		_enumerate_backward_variables(sentence, beta, p_transition_tkji, _scaling, false);
		double px = _beta(0, 1, 1);
		assert(px > 0);
		return px;
	}
	// 文の可能な分割全てを考慮した文の確率（<eos>への接続を含む）
	// use_scaling=trueならアンダーフローを防ぐ
	double Lattice::compute_log_normalizing_constant(Sentence* sentence, bool use_scaling){
		_clear_word_id_cache(sentence->size());
		_clear_p_tkji(sentence->size());
		// 前向き確率を求める
		_enumerate_forward_variables(sentence, _alpha, _pw_h_tkji, _p_transition_tkji, _scaling, use_scaling);
		// <eos>へ到達する確率を全部足す
		int t = sentence->size() + 1;	// <eos>
		int k = 1;	// <eos>の長さは1
		if(use_scaling){
			// スケーリング係数を使う場合は逆数の積がそのまま文の確率になる
			double log_px = 0;
			for(int m = 1;m <= t;m++){
				log_px += log(1.0 / _scaling[m]);
			}
			return log_px;
		}
		double px = 0;
		for(int j = 1;j <= std::min(t - k, _max_word_length);j++){
			px += _alpha(t, k, j);
		}
		return log(px);
	}
	void Lattice::_enumerate_forward_variables(Sentence* sentence, mat::tri<double> &alpha, mat::quad<double> &pw_h_tkji, mat::quad<double> &p_transition_tkji, array<double> &scaling, bool use_scaling){
		assert(sentence->size() <= _max_sentence_length);
		int size = sentence->size() + 1;
		#ifdef __DEBUG__
			// 変な値を入れる
			for(int t = 0;t < size;t++){
				if(use_scaling){
					scaling[t] = -1;
				}
				for(int k = 0;k < _max_word_length + 1;k++){
					for(int j = 0;j < _max_word_length + 1;j++){
						alpha(t, k, j) = -1;
					}
				}
			}
		#endif 
		// <eos>未満の前向き確率を計算
		alpha(0, 0, 0) = 1;
		if(use_scaling){
			scaling[0] = 1;
		}
		for(int t = 1;t <= sentence->size();t++){
			double prod_scaling = 1;
			for(int k = 1;k <= std::min(t, _max_word_length);k++){
				if(use_scaling == true && k > 1){
					prod_scaling *= scaling[t - k + 1];
				}
				// CRFのポテンシャルはtとkのみで決まるため先に計算しておく
				double crf_potential = 0;
				if(_pure_npylm_mode == false){
					crf_potential = _crf->compute_gamma(sentence, t - k + 1, t + 1);
				}
				for(int j = (t - k == 0) ? 0 : 1;j <= std::min(t - k, _max_word_length);j++){
					_sum_alpha_t_k_j(sentence, t, k, j, alpha, pw_h_tkji, p_transition_tkji, prod_scaling, crf_potential);
				}
			}
			// スケーリング
			if(use_scaling == true){
				double sum_alpha = 0;
				for(int k = 1;k <= std::min(t, _max_word_length);k++){
					for(int j = (t - k == 0) ? 0 : 1;j <= std::min(t - k, _max_word_length);j++){
						assert(alpha(t, k, j) > 0);
						sum_alpha += alpha(t, k, j);
					}
				}
				assert(sum_alpha > 0);
				scaling[t] = 1.0 / sum_alpha;
				for(int k = 1;k <= std::min(t, _max_word_length);k++){
					for(int j = (t - k == 0) ? 0 : 1;j <= std::min(t - k, _max_word_length);j++){
						alpha(t, k, j) *= scaling[t];
					}
				}
			}
		}
		// <eos>への接続を考える
		double alpha_eos = 0;
		int t = sentence->size() + 1; // <eos>を指す
		int k = 1;	// ここでは<eos>の長さを1と考える
		wchar_t const* characters = sentence->_characters;
		array<int> &character_ids = sentence->_character_ids;
		int character_ids_length = sentence->size();
		double potential = (_pure_npylm_mode == true) ? 0 : _crf->compute_gamma(sentence, t, t + 1);
		// double potential = 0;
		for(int j = 1;j <= std::min(t - k, _max_word_length);j++){
			double sum_prob = 0;
			for(int i = (t - k - j == 0) ? 0 : 1;i <= std::min(t - k - j, _max_word_length);i++){
				double p_transition = 0;
				if(_pure_crf_mode){
					p_transition = exp(potential);
				}else{
					_word_ids[0] = get_substring_word_id_at_t_k(sentence, t - k - j, i);
					_word_ids[1] = get_substring_word_id_at_t_k(sentence, t - k, j);
					_word_ids[2] = SPECIAL_CHARACTER_END;
					double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2);
					assert(pw_h > 0);
					p_transition = (_pure_npylm_mode) ? pw_h : exp(_lambda_0() * log(pw_h) + potential);
					pw_h_tkji(t, k, j, i) = pw_h;
					p_transition_tkji(t, k, j, i) = p_transition;
				}
				assert(p_transition > 0);
				sum_prob += p_transition * alpha(t - k, j, i);
			}
			alpha(t, k, j) = sum_prob;
			alpha_eos += sum_prob;
		}
		if(use_scaling){
			scaling[t] = 1.0 / alpha_eos;
		}
	}
	void Lattice::_enumerate_backward_variables(Sentence* sentence, mat::tri<double> &beta, mat::quad<double> &p_transition_tkji, array<double> &scaling, bool use_scaling){
		assert(sentence->size() <= _max_sentence_length);
		int size = sentence->size() + 1;
		#ifdef __DEBUG__
			for(int t = 0;t < size;t++){
				for(int k = 0;k < _max_word_length + 1;k++){
					for(int j = 0;j < _max_word_length + 1;j++){
						beta(t, k, j) = -1;	// 変な値を入れる
					}
				}
			}
		#endif 
		// if(use_scaling){
		// 	scaling[0] = 1;
		// }
		// <eos>への接続を考える
		int t = sentence->size();
		wchar_t const* characters = sentence->_characters;
		array<int> &character_ids = sentence->_character_ids;
		int character_ids_length = sentence->size();
		double potential = (_pure_npylm_mode == true) ? 0 : _crf->compute_gamma(sentence, t + 1, t + 2);
		// double potential = 0;
		for(int k = 1;k <= std::min(t, _max_word_length);k++){
			id word_k_id = get_substring_word_id_at_t_k(sentence, t, k);
			for(int j = (t - k == 0) ? 0 : 1;j <= std::min(t - k, _max_word_length);j++){
				double p_transition = 0;
				if(_pure_crf_mode){
					p_transition = exp(potential);
				}else{
					if(p_transition_tkji(t + 1, 1, k, j) > 0){
						p_transition = p_transition_tkji(t + 1, 1, k, j);
						#ifdef __DEBUG__
							_word_ids[0] = get_substring_word_id_at_t_k(sentence, t - k, j);
							_word_ids[1] = word_k_id;
							_word_ids[2] = SPECIAL_CHARACTER_END;
							double _pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2);
							assert(_pw_h > 0);
							double _p_transition = exp(_lambda_0() * log(_pw_h) + potential);
							assert(p_transition == _p_transition);
						#endif 
					}else{
						_word_ids[0] = get_substring_word_id_at_t_k(sentence, t - k, j);
						_word_ids[1] = word_k_id;
						_word_ids[2] = SPECIAL_CHARACTER_END;
						double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2);
						assert(pw_h > 0);
						p_transition = (_pure_npylm_mode) ? pw_h : exp(_lambda_0() * log(pw_h) + potential);
					}
				}
				assert(p_transition > 0);
				if(use_scaling){
					p_transition *= scaling[t + 1];
				}
				beta(t, k, j) = p_transition;
			}
		}
		// それ以外の場合
		for(int t = sentence->size() - 1;t >= 1;t--){
			for(int k = 1;k <= std::min(t, _max_word_length);k++){
				for(int j = (t - k == 0) ? 0 : 1;j <= std::min(t - k, _max_word_length);j++){
					beta(t, k, j) = 0;
					_sum_beta_t_k_j(sentence, t, k, j, beta, p_transition_tkji, scaling, use_scaling);
				}
			}
		}
		// t=0, k=1, j=1
		// <bos>2つが文脈になる
		double beta_0_1_1 = 0;
		for(int i = 1;i <= std::min(sentence->size(), _max_word_length);i++){
			double prod_scaling = 1;
			if(use_scaling){
				for(int m = 1;m <= i;m++){
					prod_scaling *= scaling[m];
				}
			}
			double p_transition = 0;
			double potential = 0;
			if(_pure_crf_mode){
				potential = _crf->compute_gamma(sentence, 1, i + 1);
				p_transition = exp(potential);
			}else{
				if(_pure_npylm_mode == false){
					potential = _crf->compute_gamma(sentence, 1, i + 1);
				}
				_word_ids[0] = SPECIAL_CHARACTER_BEGIN;
				_word_ids[1] = SPECIAL_CHARACTER_BEGIN;
				_word_ids[2] = get_substring_word_id_at_t_k(sentence, i, i);
				double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2, 0, i - 1);
				assert(pw_h > 0);
				p_transition = (_pure_npylm_mode) ? pw_h : exp(_lambda_0() * log(pw_h) + potential);
			}
			assert(p_transition > 0);
			beta_0_1_1 += _beta(i, i, 0) * p_transition * prod_scaling;
		}
		_beta(0, 1, 1) = beta_0_1_1;
	}
	// 後ろ向き確率を計算
	// 正規化定数をここでは掛けないことに注意
	// pw_h_tkjiは前向き確率計算時にキャッシュされている（-1が入っている場合再計算する）
	void Lattice::_sum_beta_t_k_j(Sentence* sentence, int t, int k, int j, mat::tri<double> &beta, mat::quad<double> &p_transition_tkji, array<double> &scaling, bool use_scaling){
		id word_k_id = get_substring_word_id_at_t_k(sentence, t, k);
		wchar_t const* characters = sentence->_characters;
		array<int> &character_ids = sentence->_character_ids;
		int character_ids_length = sentence->size();
		assert(1 <= t && t <= character_ids_length);
		assert(1 <= k && k <= _max_word_length);
		assert(0 <= j && j <= _max_word_length);
		assert(t - k >= 0);
		assert(t < sentence->size());
		// それ以外の場合は周辺化
		double sum = 0;
		double prod_scaling = 1;
		for(int i = 1;i <= std::min(sentence->size() - t, _max_word_length);i++){
			if(use_scaling){
				prod_scaling *= scaling[t + i];
			}
			id word_i_id = get_substring_word_id_at_t_k(sentence, t + i, i);
			id word_j_id = get_substring_word_id_at_t_k(sentence, t - k, j);
			_word_ids[0] = word_j_id;
			_word_ids[1] = word_k_id;
			_word_ids[2] = word_i_id;
			double p_transition = 0;
			double potential = 0;
			if(_pure_crf_mode){
				potential = _crf->compute_gamma(sentence, t + 1, t + i + 1);
				p_transition = exp(potential);
			}else{
				if(_pure_npylm_mode == false){
					potential = _crf->compute_gamma(sentence, t + 1, t + i + 1);
				}
				if(p_transition_tkji(t + i, i, k, j) > 0){
					p_transition = p_transition_tkji(t + i, i, k, j);
				}else{
					double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2, t, t + i - 1);
					assert(pw_h > 0);
					p_transition = (_pure_npylm_mode) ? pw_h : exp(_lambda_0() * log(pw_h) + potential);
				}
			}
			assert(p_transition > 0);
			assert(beta(t + i, i, k) > 0);
			double value = p_transition * beta(t + i, i, k) * prod_scaling;
			assert(p_transition > 0);

			#ifdef __DEBUG__
				if(_pure_crf_mode == false){
					if(p_transition_tkji(t + i, i, k, j) > 0){
						double pw_h = _npylm->compute_p_w_given_h(character_ids, characters, character_ids_length, _word_ids, 3, 2, t, t + i - 1);
						assert(pw_h > 0);
						double p_transition = exp(_lambda_0() * log(pw_h) + potential);
						assert(p_transition == p_transition_tkji(t + i, i, k, j));
					}
				}
				if(value <= 0){
					std::cout << value << std::endl;
					std::cout << prod_scaling << std::endl;
					// std::cout << pw_h << std::endl;
					std::cout << beta(t + i, i, k) << std::endl;
					std::cout << t << ", " << k << ", " << j << ", " << i << std::endl;
				}
			#endif

			assert(value > 0);
			sum += value;
			// assert(_p_transition_tkji(t + i, i, k, j) == pw_h);
		}
		assert(sum > 0);
		beta(t, k, j) = sum;
		return;
	}
	// 文の部分文字列が単語になる確率
	// P_{CONC}(c_{t-k}^t|x)
	void Lattice::_enumerate_marginal_p_substring_given_sentence(mat::bi<double> &pc_s, int sentence_length, mat::tri<double> &alpha, mat::tri<double> &beta){
		assert(sentence_length <= _max_sentence_length);
		int size = sentence_length + 1;
		#ifdef __DEBUG__
			for(int t = 0;t < size;t++){
				for(int k = 0;k < _max_word_length + 1;k++){
						pc_s(t, k) = -1;
				}
			}
		#endif 
		for(int t = 1;t <= sentence_length;t++){
			for(int k = 1;k <= std::min(t, _max_word_length);k++){
				// jを網羅する
				double sum_probability = 0;
				for(int j = (t - k == 0) ? 0 : 1;j <= std::min(t - k, _max_word_length);j++){
					assert(alpha(t, k, j) > 0);
					assert(beta(t, k, j) > 0);
					sum_probability += alpha(t, k, j) * beta(t, k, j);
				}
				if(sum_probability > 1){	// 多少の誤差は丸める
					assert(sum_probability - 1 < 1e-12);
					sum_probability = 1;
				}
				assert(0 < sum_probability && sum_probability <= 1);
				pc_s(t, k) = sum_probability;
			}
		}
	}
	// Pconc(c^{t-k-j}_{t-k-j-i+1}, c^{t-k}_{t-k-j+1}, c^t_{t-k+1}|x)の計算
	void Lattice::enumerate_marginal_p_trigram_given_sentence(Sentence* sentence, mat::quad<double> &p_conc_tkji, mat::quad<double> &pw_h_tkji, bool use_scaling){
		reserve(_max_word_length, sentence->size());
		_clear_word_id_cache(sentence->size());
		_clear_p_tkji(sentence->size());
		_enumerate_forward_variables(sentence, _alpha, pw_h_tkji, _p_transition_tkji, _scaling, use_scaling);
		_enumerate_backward_variables(sentence, _beta, _p_transition_tkji, _scaling, use_scaling);
		_enumerate_marginal_p_trigram_given_sentence(sentence, p_conc_tkji, _alpha, _beta, _p_transition_tkji, _scaling, use_scaling);
	}	
	void Lattice::_enumerate_marginal_p_trigram_given_sentence(Sentence* sentence, mat::quad<double> &p_conc, mat::tri<double> &alpha, mat::tri<double> &beta, mat::quad<double> &p_transition_tkji, array<double> &scaling, bool use_scaling){
		assert(_pure_crf_mode == false);
		for(int t = 1;t <= sentence->size();t++){
			for(int k = 1;k <= std::min(t, _max_word_length);k++){
				double prod_scaling = 1;
				if(use_scaling == true){
					for(int m = t - k + 1;m <= t;m++){
						prod_scaling *= scaling[m];
					}
				}
				for(int j = (t - k == 0) ? 0 : 1;j <= std::min(t - k, _max_word_length);j++){
					for(int i = (t - k - j == 0) ? 0 : 1;i <= std::min(t - k - j, _max_word_length);i++){
						assert(p_transition_tkji(t, k, j, i) > 0);
						assert(alpha(t - k, j, i) > 0);
						assert(beta(t, k, j) > 0);
						double marginal_p = alpha(t - k, j, i) * beta(t, k, j) * p_transition_tkji(t, k, j, i) * prod_scaling;
						if(marginal_p > 1){		// 多少の誤差は丸める
							assert(marginal_p - 1 < 1e-12);
							marginal_p = 1;
						}else{
							assert(0 < marginal_p && marginal_p <= 1);
						}
						p_conc(t, k, j, i) = marginal_p;
					}
				}
			}
		}

		int t = sentence->size() + 1;
		int k = 1;
		double prod_scaling = 1;
		if(use_scaling == true){
			for(int m = t - k + 1;m <= t;m++){
				prod_scaling *= scaling[m];
			}
		}
		for(int j = (t - k == 0) ? 0 : 1;j <= std::min(t - k, _max_word_length);j++){
			for(int i = (t - k - j == 0) ? 0 : 1;i <= std::min(t - k - j, _max_word_length);i++){
				assert(p_transition_tkji(t, k, j, i) > 0);
				double marginal_p = alpha(t - k, j, i) * p_transition_tkji(t, k, j, i) * prod_scaling;
				assert(0 < marginal_p && marginal_p <= 1);
				// std::cout << "t = " << t << ", k = " << k << ", j = " << j << ", i = " << i << ", pw_h = " << p_transition_tkji(t, k, j, i) << ", marginal_p = " << marginal_p << ", alpha = " << alpha(t - k, j, i) << ", beta = " <<  beta(t, k, j) << ", prod_scaling = " << prod_scaling << std::endl;
				p_conc(t, k, j, i) = marginal_p;
			}
		}
	}
	// p(z_t, z_{t+1}|s)の計算
	void Lattice::enumerate_marginal_p_z_given_sentence(Sentence* sentence, mat::tri<double> &pz_s){
		reserve(_max_word_length, sentence->size());
		_clear_word_id_cache(sentence->size());
		_clear_p_tkji(sentence->size());
		_enumerate_forward_variables(sentence, _alpha, _pw_h_tkji, _p_transition_tkji, _scaling, true);
		_enumerate_backward_variables(sentence, _beta, _p_transition_tkji, _scaling, true);
		_enumerate_marginal_p_z_given_sentence(sentence, pz_s, _alpha, _beta);
	}
	void Lattice::_enumerate_marginal_p_z_given_sentence(Sentence* sentence, mat::tri<double> &pz_s, mat::tri<double> &alpha, mat::tri<double> &beta){
		_enumerate_marginal_p_substring_given_sentence(_pc_s, sentence->size(), alpha, beta);
		_enumerate_marginal_p_z_given_sentence_using_p_substring(pz_s, sentence->size(), _pc_s);
	}
	void Lattice::enumerate_marginal_p_z_and_trigram_given_sentence(Sentence* sentence, mat::quad<double> &p_conc_tkji, mat::quad<double> &pw_h_tkji, mat::tri<double> &pz_s){
		reserve(_max_word_length, sentence->size());
		_clear_word_id_cache(sentence->size());
		_p_transition_tkji.fill(-1, sentence->size());
		pw_h_tkji.fill(-1, sentence->size());
		_enumerate_forward_variables(sentence, _alpha, pw_h_tkji, _p_transition_tkji, _scaling, true);
		_enumerate_backward_variables(sentence, _beta, _p_transition_tkji, _scaling, true);
		_enumerate_marginal_p_z_given_sentence(sentence, pz_s, _alpha, _beta);
		_enumerate_marginal_p_trigram_given_sentence(sentence, p_conc_tkji, _alpha, _beta, _p_transition_tkji, _scaling, true);
	}
	// p(z_t, z_{t+1}|s)の計算
	// Zsは統合モデル上での文の確率
	void Lattice::_enumerate_marginal_p_z_given_sentence_using_p_substring(mat::tri<double> &pz_s, int sentence_length, mat::bi<double> &pc_s){
		assert(sentence_length <= _max_sentence_length);
		pz_s(0, 0, 0) = 0;
		pz_s(0, 0, 1) = 0;
		pz_s(0, 1, 0) = 0;
		pz_s(0, 1, 1) = 1;
		for(int t = 1;t <= sentence_length;t++){
			// std::cout << "z" << t << ", z" << (t + 1) << std::endl;
			pz_s(t, 1, 1) = _compute_p_z_case_1_1(sentence_length, t, pc_s);
			pz_s(t, 1, 0) = _compute_p_z_case_1_0(sentence_length, t, pc_s);
			pz_s(t, 0, 1) = _compute_p_z_case_0_1(sentence_length, t, pc_s);
			pz_s(t, 0, 0) = std::max(0.0, 1.0 - pz_s(t, 1, 1) - pz_s(t, 1, 0) - pz_s(t, 0, 1));
			#ifdef __DEBUG__
				// std::cout << "1-1: " << pz_s(t, 1, 1] << std::endl;
				// std::cout << "1-0: " << pz_s(t, 1, 0] << std::endl;
				// std::cout << "0-1: " << pz_s(t, 0, 1] << std::endl;
				// std::cout << "0-0: " << pz_s(t, 0, 0] << std::endl;
				// std::cout << "*-* - 0-0: " << pz_s(t, 1, 1] + pz_s(t, 1, 0] + pz_s(t, 0, 1] << std::endl;
				double p_0_0 = _compute_p_z_case_0_0(sentence_length, t, pc_s);
				// std::cout << "0-0*: " << p_0_0 << std::endl;
				assert(std::abs(p_0_0 - pz_s(t, 0, 0)) < 1e-12);
				if(1 < t && t < sentence_length){
					if(pz_s(t, 0, 0) <= 0){
						std::cout << "t: " << t << std::endl;
						std::cout << "1-1: " << pz_s(t, 1, 1) << std::endl;
						std::cout << "1-0: " << pz_s(t, 1, 0) << std::endl;
						std::cout << "0-1: " << pz_s(t, 0, 1) << std::endl;
						std::cout << "0-0: " << pz_s(t, 0, 0) << std::endl;
						std::cout << "*-* - 0-0: " << pz_s(t, 1, 1) + pz_s(t, 1, 0) + pz_s(t, 0, 1) << std::endl;
					}
					// assert(pz_s(t, 0, 0] > 0);
				}
			#endif
		}
		// 2つめの<eos>への遷移
		pz_s(sentence_length + 1, 0, 0) = 0;
		pz_s(sentence_length + 1, 0, 1) = 0;
		pz_s(sentence_length + 1, 1, 0) = 0;
		pz_s(sentence_length + 1, 1, 1) = 1;
		// std::cout << "1-1: " << pz_s(sentence_length, 1, 1] << std::endl;
		// std::cout << "1-0: " << pz_s(sentence_length, 1, 0] << std::endl;
		// std::cout << "0-1: " << pz_s(sentence_length, 0, 1] << std::endl;
		// std::cout << "0-0: " << pz_s(sentence_length, 0, 0] << std::endl;
		assert(pz_s(sentence_length, 0, 0) < 1e-12);
		assert(pz_s(sentence_length, 1, 0) < 1e-12);
		if(sentence_length > 1){
			assert(pz_s(sentence_length, 0, 1) > 0);
		}
		assert(pz_s(sentence_length, 1, 1) > 0);
	}
	double Lattice::_compute_p_z_case_1_1(int sentence_length, int t, mat::bi<double> &pc_s){
		assert(0 <= pc_s(t, 1) && pc_s(t, 1) <= 1);
		// std::cout << "		pc_s[" << t << "][1] = " << pc_s(t, 1) << std::endl;
		return pc_s(t, 1);
	}
	double Lattice::_compute_p_z_case_1_0(int sentence_length, int t, mat::bi<double> &pc_s){
		if(t == sentence_length){
			return 0;
		}
		double p_1_0 = 0;
		// std::cout << "	case 1-0:" << std::endl;
		for(int j = 2;j <= std::min(sentence_length - t + 1, _max_word_length);j++){
			// std::cout << "		pc_s[" << (t + j - 1) << "][" << j << "] = " << pc_s(t + j - 1, j) << std::endl;
			assert(0 <= pc_s(t + j - 1, j) && pc_s(t + j - 1, j) <= 1);
			p_1_0 += pc_s(t + j - 1, j);
		}
		return p_1_0;
	}
	double Lattice::_compute_p_z_case_0_1(int sentence_length, int t, mat::bi<double> &pc_s){
		double p_0_1 = 0;
		// std::cout << "	case 0-1:" << std::endl;
		for(int j = 2;j <= std::min(t, _max_word_length);j++){
			// std::cout << "		pc_s[" << t << "][" << j << "] = " << pc_s(t, j) << std::endl;
			assert(0 <= pc_s(t, j) && pc_s(t, j) <= 1);
			p_0_1 += pc_s(t, j);
		}
		return p_0_1;
	}
	double Lattice::_compute_p_z_case_0_0(int sentence_length, int t, mat::bi<double> &pc_s){
		if(t == 1){
			return 0;
		}
		double p_0_0 = 0;
		// std::cout << "	case 0-0:" << std::endl;
		for(int k = 1;k <= std::min(sentence_length - t, _max_word_length - 2);k++){
			for(int j = k + 2;j <= std::min(t + k, _max_word_length);j++){
				// std::cout << "		pc_s[" << (t + k) << "][" << j << "] = " << pc_s(t + k, j) << std::endl;
				assert(0 < pc_s(t + k, j) && pc_s(t + k, j) <= 1);
				p_0_0 += pc_s(t + k, j);
			}
		}
		return p_0_0;
	}
	void Lattice::_clear_p_tkji(int N){
		_p_transition_tkji.fill(-1, N + 1);
		_pw_h_tkji.fill(-1, N + 1);
	}
	void Lattice::_clear_word_id_cache(int N){
		_substring_word_id_cache.fill(0, N + 1);
	}
	
} // namespace npylm