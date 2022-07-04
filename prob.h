#ifndef __PROB_H__
#define __PROB_H__

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

union double_format {
  double d;
  struct {
    unsigned f1 : 32;
    unsigned f0 : 20;
    unsigned e : 11;
    unsigned s : 1;
  };
};

template <typename Iter> double summation(const Iter first, const Iter last) {
  double sum = 0;
  for (; first != last; ++first)
    sum += *first;
  return sum;
}

template <typename Iter, typename BinaryOperation>
double summation(const Iter first, const Iter last, BinaryOperation op) {
  double sum = 0;
  for (; first != last; ++first)
    sum = op(*first, sum);
  return sum;
}

template <typename Iter> double average(const Iter first, const Iter last) {
  double sum = 0;
  unsigned int n = 0;
  for (; first != last; ++first, ++n)
    sum += *first;
  return sum / n;
}

template <typename Iter, typename BinaryOperation>
double average(const Iter first, const Iter last, BinaryOperation op) {
  double sum = 0;
  unsigned int n = 0;
  for (; first != last; ++first, ++n)
    sum = op(*first, sum);
  return sum / n;
}

template <typename Iter> double variance(const Iter first, const Iter last) {
  using T = typename std::remove_reference<decltype(*first)>::type;
  double avg = average(first, last);
  return average(first, last, [avg](T x, double p) {
    double tmp = (x - avg);
    return p + tmp * tmp;
  });
}

template <typename Iter, typename BinaryOperation>
double variance(const Iter first, const Iter last, BinaryOperation op) {
  using T = typename std::remove_reference<decltype(*first)>::type;
  double avg = average(first, last);
  return average(first, last, [avg, &op](T x, double p) {
    double tmp = op(x, -avg);
    return p + tmp * tmp;
  });
}

template <typename Iter>
double stddeviation(const Iter first, const Iter last) {
  return std::sqrt(variance(first, last));
}

template <typename Iter, typename BinaryOperation>
double stddeviation(const Iter first, const Iter last, BinaryOperation op) {
  return std::sqrt(variance(first, last, op));
}

/**
 * @brief Generates random number according zipfian distribution.
 * It is defined as: P(X=k)= C / k^p, 1 <= k <= n
 */
class zipf_distribution {
public:
  zipf_distribution(uint32_t n, double q = 1.0) : _n(n), _q(q) {
    std::vector<double> pdf(n);
    for (uint32_t i = 0; i < n; ++i) {
      pdf[i] = std::pow((double)i + 1, -q);
    }
    _dist = std::discrete_distribution<uint32_t>(pdf.begin(), pdf.end());
  }

  template <typename Generator> uint32_t operator()(Generator &g) {
    return _dist(g) + 1;
  }

  uint32_t min() { return 1; }
  uint32_t max() { return _n; }

private:
  uint32_t _n;
  double _q;
  std::discrete_distribution<uint32_t> _dist;
};

#endif // __PROB_H__