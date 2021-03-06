#pragma once
#include <iostream>
#include <cassert>
#include <execinfo.h>
#include <stdio.h>

namespace npycrf {
	namespace index {
		inline void check(int i, int size){
			if(0 <= i && i < size){
				return;
			}
			std::cout << i << " < " << size << std::endl;
			void* callstack[128];
			int frames = backtrace(callstack, 128);
			char** strs = backtrace_symbols(callstack, frames);
			for (int l = 0; l < frames; ++l) {
				printf("%s\n", strs[l]);
			}
			free(strs);
			assert(0 <= i && i < size);
		}
	}
	namespace mat {
		template<typename T>
		class bi {
		private:
			void _delete(){
				for(int t = 0;t < _t_size;t++){
					delete[] _array[t];
				}
				delete[] _array;
			}
			void _alloc(){
				_array = new T*[_t_size];
				for(int t = 0;t < _t_size;t++){
					_array[t] = new T[_k_size];
					for(int k = 0;k < _k_size;k++){
						_array[t][k] = 0;
					}
				}
			}
		public:
			T** _array;
			int _t_size;
			int _k_size;
			bi(){
				_array = nullptr;
				_t_size = 0;
				_k_size = 0;
			}
			bi(int t_size, int k_size){
				_t_size = t_size;
				_k_size = k_size;
				_alloc();
			}
			bi(const bi &a){
				if(_array != nullptr){
					_delete();
				}
				_t_size = a._t_size;
				_k_size = a._k_size;
				_alloc();
				for(int t = 0;t < _t_size;t++){
					for(int k = 0;k < _k_size;k++){
						_array[t][k] = a._array[t][k];
					}
				}
			}
			~bi(){
				if(_array != nullptr){
					_delete();
				}
			}
			bi &operator=(const bi &a){
				if(_array != nullptr){
					_delete();
				}
				_t_size = a._t_size;
				_k_size = a._k_size;
				_alloc();
				for(int t = 0;t < _t_size;t++){
					for(int k = 0;k < _k_size;k++){
						_array[t][k] = a._array[t][k];
					}
				}
				return *this;
			}
			int size_t(){
				return _t_size;
			}
			int size_k(){
				return _k_size;
			}
			void fill(T value){
				fill(value, _t_size);
			}
			void fill(T value, int t_size){
				for(int t = 0;t < t_size;t++){
					for(int k = 0;k < _k_size;k++){
						_array[t][k] = value;
					}
				}
			}
			T &operator()(int t, int k) {
				assert(t < _t_size);
				assert(k < _k_size);
				return _array[t][k];
			}
			const T &operator()(int t, int k) const {
				assert(t < _t_size);
				assert(k < _k_size);
				return _array[t][k];
			}
		};
		template<typename T>
		class tri {
		private:
			void _delete(){
				for(int t = 0;t < _t_size;t++){
					for(int k = 0;k < _k_size;k++){
						delete[] _array[t][k];
					}
					delete[] _array[t];
				}
				delete[] _array;
			}
			void _alloc(){
				_array = new T**[_t_size];
				for(int t = 0;t < _t_size;t++){
					_array[t] = new T*[_k_size];
					for(int k = 0;k < _k_size;k++){
						_array[t][k] = new T[_j_size];
						for(int j = 0;j < _j_size;j++){
							_array[t][k][j] = 0;
						}
					}
				}
			}
		public:
			T*** _array;
			int _t_size;
			int _k_size;
			int _j_size;
			tri(){
				_array = nullptr;
				_t_size = 0;
				_k_size = 0;
				_j_size = 0;
			}
			tri(int t_size, int k_size, int j_size){
				_t_size = t_size;
				_k_size = k_size;
				_j_size = j_size;
				_alloc();
			}
			tri(const tri &a){
				if(_array != nullptr){
					_delete();
				}
				_t_size = a._t_size;
				_k_size = a._k_size;
				_j_size = a._j_size;
				_alloc();
				for(int t = 0;t < _t_size;t++){
					for(int k = 0;k < _k_size;k++){
						for(int j = 0;j < _j_size;j++){
							_array[t][k][j] = a._array[t][k][j];
						}
					}
				}
			}
			~tri(){
				if(_array != nullptr){
					_delete();
				}
			}
			tri &operator=(const tri &a){
				if(_array != nullptr){
					_delete();
				}
				_t_size = a._t_size;
				_k_size = a._k_size;
				_j_size = a._j_size;
				_alloc();
				for(int t = 0;t < _t_size;t++){
					for(int k = 0;k < _k_size;k++){
						for(int j = 0;j < _j_size;j++){
							_array[t][k][j] = a._array[t][k][j];
						}
					}
				}
				return *this;
			}
			int size_t(){
				return _t_size;
			}
			int size_k(){
				return _k_size;
			}
			int size_j(){
				return _j_size;
			}
			void fill(T value){
				fill(value, _t_size);
			}
			void fill(T value, int t_size){
				for(int t = 0;t < t_size;t++){
					for(int k = 0;k < _k_size;k++){
						for(int j = 0;j < _j_size;j++){
							_array[t][k][j] = value;
						}
					}
				}
			}
			T &operator()(int t, int k, int j) {
				assert(t < _t_size);
				assert(k < _k_size);
				assert(j < _j_size);
				return _array[t][k][j];
			}
			const T &operator()(int t, int k, int j) const {
				assert(t < _t_size);
				assert(k < _k_size);
				assert(j < _j_size);
				return _array[t][k][j];
			}
		};
		template<typename T>
		class quad {
		private:
			void _delete(){
				for(int t = 0;t < _t_size;t++){
					for(int k = 0;k < _k_size;k++){
						for(int j = 0;j < _j_size;j++){
							delete[] _array[t][k][j];
						}
						delete[] _array[t][k];
					}
					delete[] _array[t];
				}
				delete[] _array;
			}
			void _alloc(){
				_array = new T***[_t_size];
				for(int t = 0;t < _t_size;t++){
					_array[t] = new T**[_k_size];
					for(int k = 0;k < _k_size;k++){
						_array[t][k] = new T*[_j_size];
						for(int j = 0;j < _j_size;j++){
							_array[t][k][j] = new T[_i_size];
							for(int i = 0;i < _i_size;i++){
								_array[t][k][j][i] = 0;
							}
						}
					}
				}
			}
		public:
			T**** _array;
			int _t_size;
			int _k_size;
			int _j_size;
			int _i_size;
			quad(){
				_array = nullptr;
				_t_size = 0;
				_k_size = 0;
				_j_size = 0;
				_i_size = 0;
			}
			quad(int t_size, int k_size, int j_size, int i_size){
				_t_size = t_size;
				_k_size = k_size;
				_j_size = j_size;
				_i_size = i_size;
				_alloc();
			}
			quad(const quad &a){
				if(_array != nullptr){
					_delete();
				}
				_t_size = a._t_size;
				_k_size = a._k_size;
				_j_size = a._j_size;
				_i_size = a._i_size;
				_alloc();
				for(int t = 0;t < _t_size;t++){
					for(int k = 0;k < _k_size;k++){
						for(int j = 0;j < _j_size;j++){
							for(int i = 0;i < _i_size;i++){
								_array[t][k][j][i] = a._array[t][k][j][i];
							}
						}
					}
				}
			}
			~quad(){
				if(_array != nullptr){
					_delete();
				}
			}
			quad &operator=(const quad &a){
				if(_array != nullptr){
					_delete();
				}
				_t_size = a._t_size;
				_k_size = a._k_size;
				_j_size = a._j_size;
				_i_size = a._i_size;
				_alloc();
				for(int t = 0;t < _t_size;t++){
					for(int k = 0;k < _k_size;k++){
						for(int j = 0;j < _j_size;j++){
							for(int i = 0;i < _i_size;i++){
								_array[t][k][j][i] = a._array[t][k][j][i];
							}
						}
					}
				}
				return *this;
			}
			int size_t(){
				return _t_size;
			}
			int size_k(){
				return _k_size;
			}
			int size_j(){
				return _j_size;
			}
			int size_i(){
				return _i_size;
			}
			void fill(T value){
				fill(value, _t_size);
			}
			void fill(T value, int t_size){
				for(int t = 0;t < t_size;t++){
					for(int k = 0;k < _k_size;k++){
						for(int j = 0;j < _j_size;j++){
							for(int i = 0;i < _i_size;i++){
								_array[t][k][j][i] = value;
							}
						}
					}
				}
			}
			T &operator()(int t, int k, int j, int i) {
				assert(t < _t_size);
				assert(k < _k_size);
				assert(j < _j_size);
				assert(i < _i_size);
				return _array[t][k][j][i];
			}
			const T &operator()(int t, int k, int j, int i) const {
				assert(t < _t_size);
				assert(k < _k_size);
				assert(j < _j_size);
				assert(i < _i_size);
				return _array[t][k][j][i];
			}
		};
	}
	template<typename T>
	class array {
	private:
		T* _array;
		int _size;
	public:
		array(){
			_array = nullptr;
			_size = 0;
		}
		array(int size){
			_array = new T[size];
			_size = size;
		}
		array(const array &a){
			_size = a._size;
			if(_array != nullptr){
				delete[] _array;
			}
			_array = new T[_size];
			for(int i = 0;i < _size;i++){
				_array[i] = a._array[i];
			}
		}
		~array(){
			if(_array != nullptr){
				delete[] _array;
			}
		}
		void fill(T value){
			for(int i = 0;i < _size;i++){
				_array[i] = value;
			}
		}
		array &operator=(const array &a){
			if(_array != nullptr){
				delete[] _array;
			}
			_size = a._size;
			_array = new T[_size];
			for(int i = 0;i < _size;i++){
				_array[i] = a._array[i];
			}
			return *this;
		}
		T &operator[](int i){    // [] 演算子の多重定義
			assert(i < _size);
			return _array[i];
		}
		const T &operator[](int i) const {    // [] 演算子の多重定義
			assert(i < _size);
			return _array[i];
		}
		int size() const{
			return _size;
		}
	};
}