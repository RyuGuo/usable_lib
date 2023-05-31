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
  double avg = average(first, last, op);
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
template <typename IntType = int> class zipf_distribution {
public:
  typedef IntType result_type;

  zipf_distribution(IntType max, double theta)
      : m_max(max), m_theta(theta), m_dist(0.0, 1.0) {
    m_c = std::pow(m_max, -m_theta) / zeta(m_theta, m_max);
    m_q = std::pow(2.0, -m_theta);
    m_h = harmonic(m_max);
    m_v = m_dist(m_gen);
  }

  /**
   * @brief 返回zipf分布随机数[0, max)
   *
   * @tparam Generator
   * @param g
   * @return IntType
   */
  template <typename Generator> IntType operator()(Generator &g) {
    while (true) {
      double u = m_dist(g) - 0.5;
      double y = std::floor(std::pow(m_max + 0.5, m_v - u) - 0.5);
      if (y < 1 || y > m_max)
        continue;
      double k = std::floor(y);
      m_v = m_dist(g);
      if (m_v >= m_q * std::pow(k + 1, m_theta) / (m_h + k))
        continue;
      return static_cast<IntType>(k) - 1;
    }
  }

private:
  IntType m_max;
  double m_theta;
  double m_c;
  double m_q;
  double m_h;
  double m_v;
  std::mt19937 m_gen;
  std::uniform_real_distribution<double> m_dist;

  static double zeta(double theta, IntType n) {
    double sum = 0.0;
    for (IntType i = 1; i <= n; ++i)
      sum += std::pow(i, -theta);
    return sum;
  }

  double harmonic(IntType n) const { return m_c * zeta(m_theta, n); }
};

class Histogram {
public:
  Histogram(int nr_buckets, double min_value, double max_value)
      : nr_buckets(nr_buckets), min_value(min_value),
        max_value(max_value),
        bucket_width((max_value - min_value) / nr_buckets),
        buckets(nr_buckets, 0) {}

  ~Histogram() = default;

  void add(double value) {
    if (value < min_value || value > max_value) {
      return;
    }

    int bucket = get_bucket(value);
    ++buckets[bucket];
  }

  void clear() { std::fill(buckets.begin(), buckets.end(), 0); }

  int size() const {
    int total_count = 0;
    for (int count : buckets) {
      total_count += count;
    }
    return total_count;
  }

  int percentile(double p) const {
    if (p < 0 || p > 100) {
      // Invalid percentile value
      return -1;
    }

    int total_count = size();
    int count_so_far = 0;
    for (int i = 0; i < nr_buckets; ++i) {
      count_so_far += buckets[i];
      if (count_so_far / (double)total_count * 100 >= p) {
        return i;
      }
    }

    // Should not reach here
    return -1;
  }

  double average() const {
    double sum = 0;
    for (int i = 0; i < nr_buckets; ++i) {
      sum += get_value(i) * get_count(i);
    }
    return sum / size();
  }

  friend Histogram merge(Histogram &a, Histogram &b) {
    Histogram new_histogram(std::max(a.nr_buckets, b.nr_buckets),
                            std::min(a.min_value, b.max_value),
                            std::max(a.max_value, b.max_value));
    for (int i = 0; i < a.nr_buckets; ++i) {
      new_histogram.buckets[new_histogram.get_bucket(a.get_value(i))] +=
          a.get_count(i);
    }
    for (int i = 0; i < b.nr_buckets; ++i) {
      new_histogram.buckets[new_histogram.get_bucket(b.get_value(i))] +=
          b.get_count(i);
    }
    return new_histogram;
  }

private:
  int get_bucket(double value) const {
    return (value - min_value) / bucket_width;
  }

  double get_value(int bucket) const {
    if (bucket < 0 || bucket >= nr_buckets) {
      // Invalid bucket index
      return -1;
    }

    return min_value + bucket_width * bucket;
  }

  int get_count(int bucket) const {
    if (bucket < 0 || bucket >= nr_buckets) {
      // Invalid bucket index
      return -1;
    }

    return buckets[bucket];
  }

  const int nr_buckets;
  const double min_value;
  const double max_value;
  const double bucket_width;
  std::vector<int> buckets;
};

#endif // __STATISTIC_H__
