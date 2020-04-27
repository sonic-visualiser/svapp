/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef SV_DTW_H
#define SV_DTW_H

#include <vector>
#include <functional>

template <typename Value>
class DTW
{
public:
    DTW(std::function<double(const Value &, const Value &)> distanceMetric) :
        m_metric(distanceMetric) { }

    std::vector<size_t> alignSeries(std::vector<Value> s1,
                                    std::vector<Value> s2) {

        // Return the index into s2 for each element in s1
        
        std::vector<size_t> alignment(s1.size(), 0);

        if (s1.empty() || s2.empty()) {
            return alignment;
        }

        auto costs = costSeries(s1, s2);

        size_t j = s1.size() - 1;
        size_t i = s2.size() - 1;

        while (j > 0 && i > 0) {

            alignment[j] = i;
            
            cost_t a = costs[j-1][i];
            cost_t b = costs[j][i-1];
            cost_t both = costs[j-1][i-1];

            if (a < b) {
                --j;
                if (both < a) {
                    --i;
                }
            } else {
                --i;
                if (both < b) {
                    --j;
                }
            }
        }

        return alignment;
    }

private:
    std::function<double(const Value &, const Value &)> m_metric;
    
    typedef double cost_t;

    struct CostOption {
        bool present;
        cost_t cost;
    };

    cost_t choose(CostOption x, CostOption y, CostOption d) {
        if (x.present && y.present) {
            if (!d.present) {
                throw std::logic_error("if x & y both exist, so must diagonal");
            }
            return std::min(std::min(x.cost, y.cost), d.cost);
        } else if (x.present) {
            return x.cost;
        } else if (y.present) {
            return y.cost;
        } else {
            return 0.0;
        }
    }

    std::vector<std::vector<cost_t>> costSeries(std::vector<Value> s1,
                                                std::vector<Value> s2) {

        std::vector<std::vector<cost_t>> costs
            (s1.size(), std::vector<cost_t>(s2.size(), 0.0));

        for (size_t j = 0; j < s1.size(); ++j) {
            for (size_t i = 0; i < s2.size(); ++i) {
                cost_t c = m_metric(s1[j], s2[i]);
                costs[j][i] = choose
                    (
                        { j > 0,
                          j > 0 ? c + costs[j-1][i] : 0.0
                        },
                        { i > 0,
                          i > 0 ? c + costs[j][i-1] : 0.0
                        },
                        { j > 0 && i > 0,
                          j > 0 && i > 0 ? c + costs[j-1][i-1] : 0.0
                        });
            }
        }

        return costs;
    }
};

class MagnitudeDTW
{
public:
    MagnitudeDTW() : m_dtw(metric) { }

    std::vector<size_t> alignSeries(std::vector<double> s1,
                                    std::vector<double> s2) {
        return m_dtw.alignSeries(s1, s2);
    }

private:
    DTW<double> m_dtw;

    static double metric(const double &a, const double &b) {
        return std::abs(b - a);
    }
};

class RiseFallDTW
{
public:
    enum class Direction {
        None,
        Up,
        Down
    };
    
    struct Value {
        Direction direction;
        double distance;
    };

    RiseFallDTW() : m_dtw(metric) { }

    std::vector<size_t> alignSeries(std::vector<Value> s1,
                                    std::vector<Value> s2) {
        return m_dtw.alignSeries(s1, s2);
    }

private:
    DTW<Value> m_dtw;

    static double metric(const Value &a, const Value &b) {
        
        auto together = [](double c1, double c2) {
                            auto diff = std::abs(c1 - c2);
                            return (diff < 1.0 ? -1.0 :
                                    diff > 3.0 ?  1.0 :
                                    0.0);
                        };
        auto opposing = [](double c1, double c2) {
                            auto diff = c1 + c2;
                            return (diff < 2.0 ? 1.0 :
                                    2.0);
                        };

        if (a.direction == Direction::None || b.direction == Direction::None) {
            if (a.direction == b.direction) {
                return 0.0;
            } else {
                return 1.0;
            }
        } else {
            if (a.direction == b.direction) {
                return together (a.distance, b.distance);
            } else {
                return opposing (a.distance, b.distance);
            }
        }
    }
};

#endif
