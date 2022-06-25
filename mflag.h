/**
 * @file mflag.h

 * @brief Parse option args

 * @version 0.1
 * @date 2022-05-27
 */

#ifndef __MFLAG_H__
#define __MFLAG_H__

#include <stdexcept>
#include <string.h>
#include <string>
#include <tuple>

inline std::string __mflag_match_string(char *c, int &argc, char **argv) {
  std::string str;
  if (*c == '\"') {
    size_t len = strlen(c);
    if (*(c + len - 1) == '\"') {
      str.assign(c + 1, len - 2);
    } else {
      str += std::string(c + 1) + ' ';
      while (1) {
        ++argc;
        c = argv[argc];
        len = strlen(c);
        if (*(c + len - 1) != '\"') {
          str += std::string(c) + ' ';
        } else {
          break;
        }
      }
      str += std::string(c, len - 1);
    }
  } else {
    str = c;
  }
  return str;
}

template <typename R>
inline R mflag_match_s(const std::string &tag, int argc, char **argv);

template <>
inline bool mflag_match_s<bool>(const std::string &tag, int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "-" + tag;
    if (argv[i] == ttag) {
      return true;
    }
  }
  return false;
}

template <>
inline int mflag_match_s<int>(const std::string &tag, int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "-" + tag;
    if (argv[i] == ttag) {
      return atoi(argv[i + 1]);
    }
  }
  throw std::invalid_argument(tag);
}

template <>
inline float mflag_match_s<float>(const std::string &tag, int argc,
                                  char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "-" + tag;
    if (argv[i] == ttag) {
      return atof(argv[i + 1]);
    }
  }
  throw std::invalid_argument(tag);
}

template <>
inline std::string mflag_match_s<std::string>(const std::string &tag, int argc,
                                              char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "-" + tag;
    if (argv[i] == ttag) {
      return __mflag_match_string(argv[i + 1], i, argv);
    }
  }
  throw std::invalid_argument(tag);
}

template <typename R>
inline R mflag_match_s(const std::string &tag, int argc, char **argv,
                       const R &default_val);

template <>
inline int mflag_match_s<int>(const std::string &tag, int argc, char **argv,
                              const int &default_val) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "-" + tag;
    if (argv[i] == ttag) {
      return atoi(argv[i + 1]);
    }
  }
  return default_val;
}

template <>
inline float mflag_match_s<float>(const std::string &tag, int argc, char **argv,
                                  const float &default_val) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "-" + tag;
    if (argv[i] == ttag) {
      return atof(argv[i + 1]);
    }
  }
  return default_val;
}

template <>
inline std::string mflag_match_s<std::string>(const std::string &tag, int argc,
                                              char **argv,
                                              const std::string &default_val) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "-" + tag;
    if (argv[i] == ttag) {
      return __mflag_match_string(argv[i + 1], i, argv);
    }
  }
  return default_val;
}

template <typename R>
inline R mflag_match(const std::string &tag, int argc, char **argv);

template <>
inline int mflag_match<int>(const std::string &tag, int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "--" + tag + "=";
    if (memcmp(argv[i], ttag.c_str(), ttag.size()) == 0) {
      return atoi(argv[i] + ttag.size());
    }
  }
  throw std::invalid_argument(tag);
}

template <>
inline bool mflag_match<bool>(const std::string &tag, int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "--" + tag + "=";
    if (memcmp(argv[i], ttag.c_str(), ttag.size()) == 0) {
      return true;
    }
  }
  return false;
}

template <>
inline float mflag_match<float>(const std::string &tag, int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "--" + tag + "=";
    if (memcmp(argv[i], ttag.c_str(), ttag.size()) == 0) {
      return atof(argv[i] + ttag.size());
    }
  }
  throw std::invalid_argument(tag);
}

template <>
inline std::string mflag_match<std::string>(const std::string &tag, int argc,
                                            char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "--" + tag + "=";
    if (memcmp(argv[i], ttag.c_str(), ttag.size()) == 0) {
      return __mflag_match_string(argv[i] + ttag.size(), i, argv);
    }
  }
  throw std::invalid_argument(tag);
}

template <typename R>
inline R mflag_match(const std::string &tag, int argc, char **argv,
                     const R &default_val);

template <>
inline int mflag_match<int>(const std::string &tag, int argc, char **argv,
                            const int &default_val) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "--" + tag + "=";
    if (memcmp(argv[i], ttag.c_str(), ttag.size()) == 0) {
      return atoi(argv[i] + ttag.size());
    }
  }
  return default_val;
}

template <>
inline float mflag_match<float>(const std::string &tag, int argc, char **argv,
                                const float &default_val) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "--" + tag + "=";
    if (memcmp(argv[i], ttag.c_str(), ttag.size()) == 0) {
      return atof(argv[i] + ttag.size());
    }
  }
  return default_val;
}

template <>
inline std::string mflag_match<std::string>(const std::string &tag, int argc,
                                            char **argv,
                                            const std::string &default_val) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "--" + tag + "=";
    if (memcmp(argv[i], ttag.c_str(), ttag.size()) == 0) {
      return __mflag_match_string(argv[i] + ttag.size(), i, argv);
    }
  }
  return default_val;
}

template <std::size_t N> struct __mflag_match_all_helper {
  template <typename T, typename R>
  static void match(T &t, R &tags, int argc, char **argv) {
    std::string tag = std::get<N - 1>(tags).first;
    using U = decltype(std::get<N - 1>(tags).second);
    try {
      // match `-[OP]`
      std::get<N - 1>(t).second = mflag_match_s<U>(tag, argc, argv);
      std::get<N - 1>(t).first = true;
    } catch (std::exception &e) {
      try {
        // match `--[OP]=`
        std::get<N - 1>(t).second = mflag_match<U>(tag, argc, argv);
        std::get<N - 1>(t).first = true;
      } catch (std::exception &e) {
        // default value
        std::get<N - 1>(t).second = std::get<N - 1>(tags).second;
        std::get<N - 1>(t).first = false;
      }
    }
    __mflag_match_all_helper<N - 1>::match(t, tags, argc, argv);
  }
};

template <> struct __mflag_match_all_helper<0> {
  template <typename T, typename R>
  static void match(T &t, R &tags, int argc, char **argv) {}
};

/**
 * @brief parse option args
 *
 * @example

 std::tuple<pair<string, int>, pair<string, string>, pair<string, string>> t =
 {{"n", 0}, {"name", "none"}, {"birth", "22.01"}};
 auto r = mflag_match_all(t, argc, argv);  // argv: --name="Alan Turing"
 --birth=12.06

  cout << get<0>(r).first << " " << get<0>(r).second << endl; // false 0

  cout << get<1>(r).first << " " << get<1>(r).second << endl; // true Alan
 Turing

  cout << get<2>(r).first << " " << get<2>(r).second << endl; //
 true 12.06

 *
 * @tparam P
 * @param tags A tuple of pair<tag, default val>
 * @param argc
 * @param argv
 * @return A tuple of pair<bool,match type>. The first field shows that
 args match options, otherwise the second field is default value.
 */
template <typename... P>
inline auto mflag_match_all(std::tuple<P...> &tags, int argc, char **argv) {
  using T = std::tuple<std::pair<bool, typename P::second_type>...>;
  T t;
  __mflag_match_all_helper<std::tuple_size<T>::value>::match(t, tags, argc,
                                                             argv);
  return t;
}

#endif // __MFLAG_H__