/**
 * @file mflag.h

 * @brief Parse option args

 * @version 0.1
 * @date 2022-05-27
 */

#ifndef __MFLAG_H__
#define __MFLAG_H__

#include <cerrno>
#include <stdexcept>
#include <string.h>
#include <string>

template <typename R>
inline R mflag_match_s(const std::string &tag, int argc, char **argv);

template <>
inline int mflag_match_s<int>(const std::string &tag, int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "-" + tag;
    if (memcmp(argv[i], ("-" + tag).c_str(), ttag.size()) == 0) {
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
    if (memcmp(argv[i], ("-" + tag).c_str(), ttag.size()) == 0) {
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
    if (memcmp(argv[i], ("-" + tag).c_str(), ttag.size()) == 0) {
      return argv[i + 1];
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
    if (memcmp(argv[i], ("-" + tag).c_str(), ttag.size()) == 0) {
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
    if (memcmp(argv[i], ("-" + tag).c_str(), ttag.size()) == 0) {
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
    if (memcmp(argv[i], ("-" + tag).c_str(), ttag.size()) == 0) {
      return argv[i + 1];
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
    if (memcmp(argv[i], ("--" + tag + "=").c_str(), ttag.size()) == 0) {
      return atoi(argv[i] + ttag.size());
    }
  }
  throw std::invalid_argument(tag);
}

template <>
inline float mflag_match<float>(const std::string &tag, int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "--" + tag + "=";
    if (memcmp(argv[i], ("--" + tag + "=").c_str(), ttag.size()) == 0) {
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
    if (memcmp(argv[i], ("--" + tag + "=").c_str(), ttag.size()) == 0) {
      return argv[i] + ttag.size();
    }
  }
  throw std::invalid_argument(tag);
}

template <typename R>
inline R mflag_match(const std::string &tag, int argc, char **argv, const R &default_val);

template <>
inline int mflag_match<int>(const std::string &tag, int argc, char **argv, const int &default_val) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "--" + tag + "=";
    if (memcmp(argv[i], ("--" + tag + "=").c_str(), ttag.size()) == 0) {
      return atoi(argv[i] + ttag.size());
    }
  }
  return default_val;
}

template <>
inline float mflag_match<float>(const std::string &tag, int argc, char **argv, const float &default_val) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "--" + tag + "=";
    if (memcmp(argv[i], ("--" + tag + "=").c_str(), ttag.size()) == 0) {
      return atof(argv[i] + ttag.size());
    }
  }
  return default_val;
}

template <>
inline std::string mflag_match<std::string>(const std::string &tag, int argc,
                                            char **argv, const std::string &default_val) {
  for (int i = 1; i < argc; ++i) {
    std::string ttag = "--" + tag + "=";
    if (memcmp(argv[i], ("--" + tag + "=").c_str(), ttag.size()) == 0) {
      return argv[i] + ttag.size();
    }
  }
  return default_val;
}


#endif // __MFLAG_H__