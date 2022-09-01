#ifndef __STATISTIC_H__
#define __STATISTIC_H__

#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

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
    double tmp = x - avg;
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
 * It is defined as: P(X=k)= C / k^q, 1 <= k <= n
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

template <typename D> class Histogram {
public:
  Histogram(D min_, D max_, D interval = 10000)
      : min_(min_), max_(max_), interval(interval), count(0) {
    uint32_t count = (max_ - min_) / interval + 1;
    hist.assign(count, 0);
  }

  void clear() {
    uint32_t count = (max_ - min_) / interval + 1;
    hist.assign(count, 0);
    count = 0;
  }

  const std::vector<uint64_t> &get_hist() { return hist; }

  void add_sample(D d) {
    if (d < min_ || d > max_)
      throw std::out_of_range("out of range");
    uint32_t which_b = (d - min_) / interval;
    ++hist[which_b];
    ++count;
  }

  double average() {
    double S = 0;
    for (uint32_t i = 0; i < hist.size(); ++i) {
      S += hist[i] * ((i + 0.5) * interval + min_);
    }
    return S / count;
  }

  double percentage(double p) {
    uint64_t pd = 0;
    uint32_t left_border = 0, right_border = hist.size();
    for (uint32_t i = 0; i < hist.size(); ++i) {
      pd += hist[i];
      double tmp = 1.0 * pd / count;
      if (tmp <= p) {
        left_border = i;
      }
      if (tmp >= p) {
        right_border = i;
        break;
      }
    }
    return right_border * interval + min_;
  }

private:
  D min_, max_;
  D interval;
  uint64_t count;
  std::vector<uint64_t> hist;
};

template <typename D>
static Histogram<D> merge(Histogram<D> &h1, Histogram<D> &h2) {
  Histogram<D> nh(std::min(h1.min_, h2.min_), std::max(h1.max_, h2.max_),
                  std::min(h1.interval, h2.interval));
  for (uint32_t i = 0; i < h1.get_hist().size(); ++i)
    for (uint64_t j = 0; j < h1.get_hist()[i]; ++j)
      nh.add_sample((i + 0.5) * h1.interval + h1.min_);
  for (uint32_t i = 0; i < h2.get_hist().size(); ++i)
    for (uint64_t j = 0; j < h2.get_hist()[i]; ++j)
      nh.add_sample((i + 0.5) * h2.interval + h2.min_);
  return nh;
}

#endif // __STATISTIC_H__
