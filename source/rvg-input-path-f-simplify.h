#ifndef RVG_INPUT_PATH_F_SIMPLIFY_H
#define RVG_INPUT_PATH_F_SIMPLIFY_H

#include <type_traits>
#include <utility>

#include "rvg-i-input-path.h"

namespace rvg {

template <typename SINK>
class input_path_f_simplify final:
    public i_sink<input_path_f_simplify<SINK>>,
    public i_input_path<input_path_f_simplify<SINK>> {

    rvgf m_tol;
    SINK m_sink;

    rvgf m_px, m_py;

public:

    explicit input_path_f_simplify(rvgf tol, SINK &&sink):
        m_tol{tol},
        m_sink{std::forward<SINK>(sink)} {
        static_assert(meta::is_an_i_input_path<SINK>::value,
            "sink is not an i_input_path");
    }

private:

    void close(rvgf x, rvgf y) {
        if (m_px != x || m_py != y)
            m_sink.linear_segment(m_px, m_py, x, y);
    }

    void advance(rvgf x, rvgf y) {
        m_px = x; m_py = y;
    }

friend i_input_path<input_path_f_simplify<SINK>>;

    void do_begin_contour(rvgf x0, rvgf y0) {
        m_sink.begin_contour(x0, y0);
        advance(x0, y0);
    }

    void do_end_open_contour(rvgf x0, rvgf y0) {
        close(x0, y0);
        m_sink.end_open_contour(x0, y0);
    }

    void do_end_closed_contour(rvgf x0, rvgf y0) {
        close(x0, y0);
        return m_sink.end_closed_contour(x0, y0);
    }

    void do_linear_segment(rvgf x0, rvgf y0, rvgf x1, rvgf y1) {
        if (std::fabs(x0-x1) > m_tol || std::fabs(y0-y1) > m_tol) {
            return m_sink.linear_segment(x0, y0, x1, y1);
        }
        advance(x1, y1);
    }

    void do_quadratic_segment(rvgf x0, rvgf y0, rvgf x1, rvgf y1,
        rvgf x2, rvgf y2) {
        m_sink.quadratic_segment(x0, y0, x1, y1, x2, y2);
        advance(x2, y2);
    }

    void do_rational_quadratic_segment(rvgf x0, rvgf y0, rvgf x1, rvgf y1,
        rvgf w1, rvgf x2, rvgf y2) {
        m_sink.rational_quadratic_segment(x0, y0, x1, y1, w1, x2, y2);
        advance(x2, y2);
    }

    void do_cubic_segment(rvgf x0, rvgf y0, rvgf x1, rvgf y1,
        rvgf x2, rvgf y2, rvgf x3, rvgf y3) {
        m_sink.cubic_segment(x0, y0, x1, y1, x2, y2, x3, y3);
        advance(x3, y3);
    }

};

template <typename SINK>
auto make_input_path_f_simplify(rvgf tol, SINK &&sink) {
    return input_path_f_simplify<SINK>{tol, std::forward<SINK>(sink)};
}

} // namespace rvg

#endif
