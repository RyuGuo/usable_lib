#ifndef __FUNCTION_TRAITS_H__
#define __FUNCTION_TRAITS_H__

#include <type_traits>
#include <utility>

template <typename R, typename... Args> struct function_traits_helper {
  static constexpr std::size_t count = sizeof...(Args);
  using result_type = R;
  using args_tuple_type = std::tuple<Args...>;
  template <std::size_t N>
  using args_type = typename std::tuple_element<N, std::tuple<Args...>>::type;
};

template <typename T> struct function_traits;
template <typename R, typename... Args>
struct function_traits<R(Args...)> : public function_traits_helper<R, Args...> {
};
template <typename R, typename... Args>
struct function_traits<R(Args...) const>
    : public function_traits_helper<R, Args...> {};
template <typename R, typename... Args>
struct function_traits<R (*)(Args...)>
    : public function_traits_helper<R, Args...> {};
template <typename R, typename... Args>
struct function_traits<R (&)(Args...)>
    : public function_traits_helper<R, Args...> {};
template <typename CType, typename R, typename... Args>
struct function_traits<R (CType::*)(Args...)>
    : public function_traits_helper<R, Args...> {
  using class_type = CType;
};
template <typename CType, typename R, typename... Args>
struct function_traits<R (CType::*)(Args...) const>
    : public function_traits_helper<R, Args...> {
  using class_type = CType;
};
template <typename T>
struct function_traits : public function_traits<decltype(&T::operator())> {};

#endif // __FUNCTION_TRAITS_H__
