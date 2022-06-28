#include <vector>

template <typename T, int Dim> struct nest_vector_helper;

template <typename T, int Dim>
struct nest_vector_helper : nest_vector_helper<T, Dim - 1> {
  using v_type = std::vector<typename nest_vector_helper<T, Dim - 1>::v_type>;
};

template <typename T> struct nest_vector_helper<T, 1> {
  using v_type = std::vector<T>;
};

template <typename T, int Dim>
struct nest_vector : nest_vector_helper<T, Dim>::v_type {};

template <typename T> struct nest_vector_translate_helper {
  static constexpr int dim = 0;
  using value_type = T;
};

template <typename V>
struct nest_vector_translate_helper<std::vector<V>>
    : nest_vector_translate_helper<V> {
  static constexpr int dim = nest_vector_translate_helper<V>::dim + 1;
  using value_type = typename nest_vector_translate_helper<V>::value_type;
};

template <typename A, typename B>
inline void do_flat(const std::vector<A> &v, std::vector<B> &r) {
  r.insert(r.end(), v.begin(), v.end());
}

template <typename A, typename B>
inline void do_flat(const std::vector<std::vector<A>> &v, std::vector<B> &r) {
  for (auto &x : v)
    do_flat(x, r);
}

template <typename A, typename B>
inline void do_flat(const std::vector<std::vector<A>> &v,
                    std::vector<std::vector<B>> &r) {
  r.resize(v.size());
  for (size_t i = 0; i < v.size(); ++i)
    do_flat(v[i], r[i]);
}

template <int N, typename V> inline auto flat(const V &v) {
  static_assert(N > 0, "N cannot be 0");
  static_assert(N < nest_vector_translate_helper<V>::dim,
                "cannot flat less dim");

  typename nest_vector_helper<
      typename nest_vector_translate_helper<V>::value_type, N>::v_type r;
  do_flat(v, r);
  return r;
}