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

class DTW
{
public:
    typedef double cost_t;

    struct CostOption {
        bool present;
        cost_t cost;
    };

    enum class Direction {
        None,
        Up,
        Down
    };
    
    struct Value {
        Direction direction;
        cost_t cost;
    };

    static cost_t choose(CostOption x, CostOption y, CostOption d) {
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

    static cost_t calculateCost(Value a, Value b) {
        auto together = [](cost_t c1, cost_t c2) {
                            auto diff = std::abs(c1 - c2);
                            return (diff < 1.0 ? -1.0 :
                                    diff > 3.0 ?  1.0 :
                                    0.0);
                        };
        auto opposing = [](cost_t c1, cost_t c2) {
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
                return together (a.cost, b.cost);
            } else {
                return opposing (a.cost, b.cost);
            }
        }
    }

    static std::vector<std::vector<cost_t>> costSeries(std::vector<Value> s1,
                                                       std::vector<Value> s2) {
        
    }
    
};

#endif
