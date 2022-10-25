#ifndef __STATISTIC_H__
#define __STATISTIC_H__

#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

template <typename T, typename Iter> T summation(Iter first, Iter last) {
  T sum = 0;
  for (; first != last; ++first)
    sum += *first;
  return sum;
}

template <typename T, typename Iter, typename BinaryOperation>
auto summation(Iter first, Iter last, BinaryOperation op) {
  T sum = 0;
  for (; first != last; ++first)
    sum = op(*first, sum);
  return sum;
}

template <typename Iter> double average(Iter first, Iter last) {
  return summation<double>(first, last) / std::distance(first, last);
}

template <typename Iter, typename BinaryOperation>
double average(Iter first, Iter last, BinaryOperation op) {
  return summation<double>(first, last, op) / std::distance(first, last);
}

template <typename Iter> double variance(Iter first, Iter last) {
  using T = decltype(*first);
  double avg = average(first, last);
  return average(first, last, [avg](const T x, const double p) {
    double tmp = x - avg;
    return p + tmp * tmp;
  });
}

template <typename Iter, typename BinaryOperation>
double variance(Iter first, Iter last, BinaryOperation op) {
  using T = decltype(*first);
  double avg = average(first, last);
  return average(first, last, [avg, &op](const T x, const double p) {
    double tmp = op(x, -avg);
    return p + tmp * tmp;
  });
}

template <typename Iter> double stddeviation(Iter first, Iter last) {
  return std::sqrt(variance(first, last));
}

template <typename Iter, typename BinaryOperation>
double stddeviation(Iter first, Iter last, BinaryOperation op) {
  return std::sqrt(variance(first, last, op));
}

/**
 * @brief Generates random number according zipfian distribution.
 * It is defined as: P(X=k)= C / k^q, 1 <= k <= n
 */
class zipf_distribution {
public:
  zipf_distribution(uint32_t n, double q = 1.0) : n_(n), q_(q) {
    std::vector<double> pdf(n);
    for (uint32_t i = 0; i < n; ++i) {
      pdf[i] = std::pow((double)i + 1, -q);
    }
    dist_ = std::discrete_distribution<uint32_t>(pdf.begin(), pdf.end());
  }

  template <typename Generator> uint32_t operator()(Generator &g) {
    return dist_(g) + 1;
  }

  uint32_t min() { return 1; }
  uint32_t max() { return n_; }

private:
  uint32_t n_;
  double q_;
  std::discrete_distribution<uint32_t> dist_;
};

template <typename D> class Histogram {
public:
  // [min, upper)
  Histogram(D min, D upper, D bcount = 10000)
      : min_(min), upper_(upper), interval_((upper - min) / bcount), count_(0) {
    hist_.assign(bcount, 0);
  }

  void clear() {
    count_ = 0;
    hist_.assign(hist_.size(), 0);
  }

  const std::vector<uint64_t> &get_hist() const { return hist_; }

  void add_sample(D d) {
    if (d < min_ || d >= upper_)
      throw std::out_of_range("out of range");
    uint32_t which_b = (d - min_) / interval_;
    ++hist_[which_b];
    ++count_;
  }

  double average() const {
    double S = 0;
    for (uint32_t i = 0; i < hist_.size(); ++i) {
      S += hist_[i] * ((i + 0.5) * interval_ + min_);
    }
    return S / count_;
  }

  double percentage(double p) const {
    uint64_t pd = 0;
    uint32_t right_border = hist_.size() - 1;
    for (uint32_t i = 0; i < hist_.size(); ++i) {
      pd += hist_[i];
      if (pd >= p * count_) {
        right_border = i;
        break;
      }
    }
    return (right_border + 0.5) * interval_ + min_;
  }

  static Histogram<D> merge(Histogram<D> &h1, Histogram<D> &h2) {
    Histogram<D> nh(std::min(h1.min_, h2.min_), std::max(h1.upper_, h2.upper_),
                    std::max(h1.hist_.size(), h2.hist_.size()));
    for (uint32_t i = 0; i < h1.hist_.size(); ++i)
      for (uint64_t j = 0; j < h1.hist_[i]; ++j)
        nh.add_sample((i + 0.5) * h1.interval_ + h1.min_);
    for (uint32_t i = 0; i < h2.hist_.size(); ++i)
      for (uint64_t j = 0; j < h2.hist_[i]; ++j)
        nh.add_sample((i + 0.5) * h2.interval_ + h2.min_);
    return nh;
  }

private:
  D min_, upper_, interval_;
  uint64_t count_;
  std::vector<uint64_t> hist_;
};

#endif // __STATISTIC_H__
