/**
 * @file csv.h

 * @brief A CSV output file stream

 * @version 0.1
 * @date 2022-05-27
 */

#ifndef __CSV_H__
#define __CSV_H__

#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

class CSVFileStream {
  std::fstream fs;
  std::string path;
  std::vector<std::string> fields;

  template <typename T> void __appendEntry(const T &t) {
    fs << t << std::endl;
  }

  template <typename T, typename... Rest>
  void __appendEntry(const T &t, const Rest &...rest) {
    fs << t << ',';
    __appendEntry(rest...);
  }

public:
  CSVFileStream(const std::string &path) : path(path) {
    if (__glibc_unlikely(access(path.c_str(), F_OK) != 0)) {
      throw "CSV file not found!";
    }

    fs.open(path.c_str());
    fs.seekg(std::ios::beg);
    std::string s;
    getline(fs, s);

    size_t b = 0, e = -1;
    while (1) {
      b = s.find_first_not_of(' ', e + 1);
      if (b == s.npos) {
        break;
      }
      e = s.find_first_of(',', e + 1);
      if (e == s.npos) {
        std::string _s = s.substr(b);
        s.erase(s.find_last_not_of(' ') + 1);
        fields.push_back(_s);
        break;
      }
      std::string _s = s.substr(b, e - b);
      s.erase(s.find_last_not_of(' ') + 1);
      fields.push_back(_s);
    }

    fs.seekg(0, std::ios::end);
    fs.seekp(0, std::ios::end);
  }

  CSVFileStream(const std::string &path, const std::vector<std::string> &fields)
      : path(path) {
    if (fields.size() == 0)
      return;
    this->fields.assign(fields.begin(), fields.end());

    fs.open(path.c_str(),
            std::ios_base::in | std::ios_base::out | std::fstream::trunc);
    fs.seekp(std::ios::beg);

    fs << fields[0];
    for (int i = 1; i < fields.size(); i++) {
      fs << ',' << fields[i];
    }
    fs << std::endl;
  }

  ~CSVFileStream() { fs.close(); }

  const std::string &getPath() { return path; }
  const std::vector<std::string> &getFields() { return fields; }

  template <typename... Args>
  void appendEntry(const Args &...args) {
    __appendEntry(args...);
  }
};

#endif // __CSV_H__