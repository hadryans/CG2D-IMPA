
#include <string>
#include <sstream>
#include <cmath>
#include <memory>
#include <assert.h>

#include <lua.h>
#include "rvg-lua.h"
#include "rvg-pngio.h"
#include "rvg-image.h"
#include "rvg-shape.h"
#include "rvg-paint.h"
#include "rvg-color-ramp.h"
#include "rvg-spread.h"
#include "rvg-rgba.h"
#include "rvg-xform.h"
#include "omp.h"

#include "rvg-winding-rule.h"
#include "rvg-i-input-path.h"
#include "rvg-i-monotonic-parameters.h"

#include "rvg-input-path-f-xform.h"
#include "rvg-input-path-f-monotonize.h"
#include "rvg-input-path-f-close-contours.h"
#include "rvg-input-path-f-downgrade-degenerate.h"

#include "blue-noise.h"
#include "hadryan-color.cpp"

#include "rvg-i-scene-data.h"
#include "rvg-lua-facade.h"

#include "rvg-driver-png.h"

#define EPS 0.00001

namespace rvg {
    namespace driver {
        namespace png {

class path_segment;

class bouding_box {
private:
    R2 m_p0;
    R2 m_p1;
public: 
    inline bouding_box()
        : m_p0(0, 0)
        , m_p1(0, 0)
    {}
    inline bouding_box(const R2 &first, const R2 &last) {
        m_p0 = make_R2(std::min(first[0], last[0]), std::min(first[1], last[1]));
        m_p1 = make_R2(std::max(first[0], last[0]), std::max(first[1], last[1]));
    }
    inline bool hit_up(const double x, const double y) const {
        (void) x;
        return y >= m_p1.get_y();
    }
    inline bool hit_down(const double x, const double y) const {
        (void) x;
        return y < m_p0.get_y();
    }
    inline bool hit_left(const double x, const double y) const {
        (void) y;
        return x <= m_p0.get_x();
    }
    inline bool hit_right(const double x, const double y) const {
        (void) y;
        return x > m_p1.get_x();
    }
    inline bool hit_inside(const double x, const double y) const {
        return y >= m_p0.get_y() && y < m_p1.get_y() && x >= m_p0.get_x() && x < m_p1.get_x();
    }
};

class path_segment {
protected:
    const R2 m_pi;
    const R2 m_pf;
    const bouding_box m_bbox; // segment bouding box
    int m_dir;
public:
    path_segment(const R2 &p0, const R2 &p1)
        : m_pi(p0)
        , m_pf(p1)
        , m_bbox(m_pi, m_pf) 
        , m_dir(1) { 
        if(m_pi[1] > m_pf[1]){
            m_dir = -1;
        }
    } 
    inline virtual ~path_segment() {
    }
    virtual bool implicit_hit(double x, double y) const = 0;
    inline bool intersect(const double x, const double y) const {
        return !(m_bbox.hit_up(x, y) || m_bbox.hit_right(x, y) || m_bbox.hit_down(x, y)) 
              &&(m_bbox.hit_left(x,  y) || implicit_hit(x, y));
    }
    inline int get_dir() const {
        return m_dir;
    }
    R2 first() const {
        return m_pi;
    }
    R2 last() const {
        return m_pf;
    }
};

class linear : public path_segment {
private:
    const R2 m_d;
public:
    inline linear(const R2 &p0, const R2 &p1)
        : path_segment(p0, p1)
        , m_d(p1-p0) 
    {}
    inline bool implicit_hit(double x, double y) const {
        return (m_d[1]*((x - m_pi[0])*m_d[1] - (y - m_pi[1])*m_d[0]) <= 0);
    }
};

class quadratic : public path_segment {
protected:
    const R2 m_p1;
    const R2 m_p2;
    const linear m_diag; 
    const bool m_cvx;
    const double m_A;
    const double m_B;
    const double m_C;
    const double m_D;
    const double m_E;
    const int m_der;
public:
    quadratic(const R2 &p0, const R2 &p1, const R2& p2, double w = 1.0) 
        : path_segment(p0, p2)
        , m_p1(p1-(p0*w))
        , m_p2(p2-p0)
        , m_diag(make_R2(0, 0), m_p2)
        , m_cvx(m_diag.implicit_hit(m_p1[0], m_p1[1]))
        , m_A(4.0*m_p1[0]*m_p1[0]-4.0*w*m_p1[0]*m_p2[0]+m_p2[0]*m_p2[0])
        , m_B(4.0*m_p1[0]*m_p2[0]*m_p1[1]-4.0*m_p1[0]*m_p1[0]*m_p2[1])
        , m_C(-4.0*m_p2[0]*m_p1[1]*m_p1[1]+4.0*m_p1[0]*m_p1[1]*m_p2[1])
        , m_D(-8.0*m_p1[0]*m_p1[1]+4.0*w*m_p2[0]*m_p1[1]+4.0*w*m_p1[0]*m_p2[1]-2.0*m_p2[0]*m_p2[1])
        , m_E(4.0*m_p1[1]*m_p1[1]-4.0*w*m_p1[1]*m_p2[1]+m_p2[1]*m_p2[1]) 
        , m_der((2*m_p2[1]*(-m_p2[0]*m_p1[1]+m_p1[0]*m_p2[1]))) 
    {}
    inline bool implicit_hit(double x, double y) const {
        x -= m_pi[0];
        y -= m_pi[1];
        bool diag_hit = m_diag.implicit_hit(x, y);
        return(m_cvx && (diag_hit && hit_me(x, y)))
           ||(!m_cvx && (diag_hit || hit_me(x, y)));
    }
    inline bool hit_me(double x, double y) const {
        return m_der*((y*(y*m_A + m_B) + x*(m_C + y*m_D + x*m_E))) <= 0;
    }
};

class cubic : public path_segment {
    double A;
    double B;
    double C;
    double D;
    double E;
    double F;
    double G;
    double H;
    double I;
    double m_der;
    std::vector<linear> m_tri; 
public:
    inline cubic(const R2 &p0, const R2 &p1, const R2 &p2, const R2 &p3)
        : path_segment(p0, p3) {
        double x1, x2, x3;
        double y1, y2, y3;
        x1 = (p1 - p0).get_x();
        y1 = (p1 - p0).get_y();
        x2 = (p2 - p0).get_x();
        y2 = (p2 - p0).get_y();
        x3 = (p3 - p0).get_x();
        y3 = (p3 - p0).get_y();
        A = -27.0*x1*x3*x3*y1*y1 + 81.0*x1*x2*x3*y1*y2 - 81.0*x1*x1*x3*y2*y2 - 
             81.0*x1*x2*x2*y1*y3 + 54.0*x1*x1*x3*y1*y3 + 81.0*x1*x1*x2*y2*y3 - 
             27.0*x1*x1*x1*y3*y3;
        B = (-27.0*x1*x1*x1 + 81.0*x1*x1*x2 - 81.0*x1*x2*x2 + 27.0*x2*x2*x2 - 27.0*x1*x1*x3 + 
              54.0*x1*x2*x3 - 27.0*x2*x2*x3 - 9.0*x1*x3*x3 + 9.0*x2*x3*x3 - 
              x3*x3*x3);
        C = 81.0*x1*x2*x2*y1 - 54.0*x1*x1*x3*y1 - 81.0*x1*x2*x3*y1 + 
            54.0*x1*x3*x3*y1 - 9.0*x2*x3*x3*y1 - 81.0*x1*x1*x2*y2 + 
            162.0*x1*x1*x3*y2 - 81.0*x1*x2*x3*y2 + 27.0*x2*x2*x3*y2 - 
            18*x1*x3*x3*y2 + 54.0*x1*x1*x1*y3 - 81.0*x1*x1*x2*y3 + 81.0*x1*x2*x2*y3 - 
            27.0*x2*x2*x2*y3 - 54.0*x1*x1*x3*y3 + 27.0*x1*x2*x3*y3;
        D = 27.0*x3*x3*y1*y1*y1 - 81.0*x2*x3*y1*y1*y2 + 81.0*x1*x3*y1*y2*y2 + 
            81.0*x2*x2*y1*y1*y3 - 54.0*x1*x3*y1*y1*y3 - 81.0*x1*x2*y1*y2*y3 + 
            27.0*x1*x1*y1*y3*y3;
        E = -81.0*x2*x2*y1*y1 + 108.0*x1*x3*y1*y1 + 81.0*x2*x3*y1*y1 - 
            54.0*x3*x3*y1*y1 - 243*x1*x3*y1*y2 + 81.0*x2*x3*y1*y2 + 
            27.0*x3*x3*y1*y2 + 81.0*x1*x1*y2*y2 + 81.0*x1*x3*y2*y2 - 54.0*x2*x3*y2*y2 - 
            108*x1*x1*y1*y3 + 243*x1*x2*y1*y3 - 81.0*x2*x2*y1*y3 - 
            9.0*x2*x3*y1*y3 - 81.0*x1*x1*y2*y3 - 81.0*x1*x2*y2*y3 + 
            54.0*x2*x2*y2*y3 + 9.0*x1*x3*y2*y3 + 54.0*x1*x1*y3*y3 - 27.0*x1*x2*y3*y3;
        F = 81.0*x1*x1*y1 - 162.0*x1*x2*y1 + 81.0*x2*x2*y1 + 54.0*x1*x3*y1 - 
            54.0*x2*x3*y1 + 9.0*x3*x3*y1 - 81.0*x1*x1*y2 + 162.0*x1*x2*y2 - 
            81.0*x2*x2*y2 - 54.0*x1*x3*y2 + 54.0*x2*x3*y2 - 9.0*x3*x3*y2 + 
            27.0*x1*x1*y3 - 54.0*x1*x2*y3 + 27.0*x2*x2*y3 + 18*x1*x3*y3 - 
            18*x2*x3*y3 + 3.0*x3*x3*y3;
        G = -54.0*x3*y1*y1*y1 + 81.0*x2*y1*y1*y2 + 81.0*x3*y1*y1*y2 - 81.0*x1*y1*y2*y2 - 
            81.0*x3*y1*y2*y2 + 27.0*x3*y2*y2*y2 + 54.0*x1*y1*y1*y3 - 162.0*x2*y1*y1*y3 + 
            54.0*x3*y1*y1*y3 + 81.0*x1*y1*y2*y3 + 81.0*x2*y1*y2*y3 - 
            27.0*x3*y1*y2*y3 - 27.0*x2*y2*y2*y3 - 54.0*x1*y1*y3*y3 + 
            18*x2*y1*y3*y3 + 9.0*x1*y2*y3*y3;
        H = -81.0*x1*y1*y1 + 81.0*x2*y1*y1 - 27.0*x3*y1*y1 + 162.0*x1*y1*y2 - 
            162.0*x2*y1*y2 + 54.0*x3*y1*y2 - 81.0*x1*y2*y2 + 81.0*x2*y2*y2 - 
            27.0*x3*y2*y2 - 54.0*x1*y1*y3 + 54.0*x2*y1*y3 - 18.0*x3*y1*y3 + 
            54.0*x1*y2*y3 - 54.0*x2*y2*y3 + 18.0*x3*y2*y3 - 9.0*x1*y3*y3 + 
            9.0*x2*y3*y3 - 3.0*x3*y3*y3;
        I = 27.0*y1*y1*y1 - 81.0*y1*y1*y2 + 81.0*y1*y2*y2 - 27.0*y2*y2*y2 + 27.0*y1*y1*y3 - 
            54.0*y1*y2*y3 + 27.0*y2*y2*y3 + 9.0*y1*y3*y3 - 9.0*y2*y3*y3 + y3*y3*y3;
        m_der = ((y1 - y2 - y3)*(-x3*x3*(4.0*y1*y1 - 2.0*y1*y2 + y2*y2) + 
                    x1*x1*(9.0*y2*y2 - 6.0*y2*y3 - 4.0*y3*y3) + 
                    x2*x2*(9.0*y1*y1 - 12*y1*y3 - y3*y3) + 
                    2*x1*x3*(-y2*(6.0*y2 + y3) + y1*(3.0*y2 + 4.0*y3)) - 
                    2*x2*(x3*(3.0*y1*y1 - y2*y3 + y1*(-6.0*y2 + y3)) + 
                    x1*(y1*(9.0*y2 - 3.0*y3) - y3*(6.0*y2 + y3))))
        );
        R2 v0(0.0, 0.0);
        R2 v1;
        R2 v2(x3, y3);
        if(std::abs(x1*x1) < EPS && std::abs(y1*y1) < EPS) {
            v1 = make_R2(x2, y2);
        }
        else if(std::abs(x3-x2)*std::abs(x3-x2) < EPS && std::abs(y3-y2)*std::abs(y3-y2) < EPS) {
            
            v1 = make_R2(x1, y1);
        }
        else if(std::abs(x2-x1)*std::abs(x2-x1) < EPS && std::abs(y2-y1)*std::abs(y2-y1) < EPS){
            v1 = make_R2(x2, y2);
        }
        else {
            v1 = make_R2(-x1*(x2*y3 - x3*y2)/(x1*y2 - x1*y3 - x2*y1 + x3*y1), 
                         -y1*(x2*y3 - x3*y2)/(x1*y2 - x1*y3 - x2*y1 + x3*y1));
        }
        m_tri.push_back(linear(v0, v1));
        m_tri.push_back(linear(v1, v2));
        m_tri.push_back(linear(v2, v0));
    }
    inline int triangle_hits(double x, double y) const {
        int sum = 0;
        for(auto &seg : m_tri) {
            if(seg.intersect(x, y)) {
                sum++;
            }
        }
        return sum;
    }
    inline bool hit_me(double x, double y) const {
        return (m_der*(y*(A + y*(y*(B) + C)) + x*(D + y*(E + y*F) + x*(G + y*H + x*I)))) <= 0;
    }
    inline bool implicit_hit(double x, double y) const {
        x -= m_pi[0];
        y -= m_pi[1];
        int hits = triangle_hits(x, y);
        return (hits == 2 || 
               (hits == 1 && hit_me(x, y)));
    }
};

class scene_object {
private:
    std::vector<path_segment*> m_path;
    e_winding_rule m_wrule;
    bouding_box m_bbox;
    color_solver* color;
public:
    inline scene_object(std::vector<path_segment*> &path, const e_winding_rule &wrule, const paint &paint_in) 
        : m_wrule(wrule) {
        m_path = path;
        R2 bb0 = path[0]->first();
        R2 bb1 = path[0]->last();
        for(auto &seg : path){
            R2 f = seg->first();
            R2 l = seg->last();
            bb0 = make_R2(std::min(bb0[0], f[0]), std::min(bb0[1], f[1]));
            bb0 = make_R2(std::min(bb0[0], l[0]), std::min(bb0[1], l[1]));
            bb1 = make_R2(std::max(bb1[0], f[0]), std::max(bb1[1], f[1]));
            bb1 = make_R2(std::max(bb1[0], l[0]), std::max(bb1[1], l[1]));
        }
        m_bbox = bouding_box(bb0, bb1);
        if(paint_in.is_solid_color()) {
            color = new color_solver(paint_in);
        } else if(paint_in.is_linear_gradient()) {
            color = new linear_gradient_solver(paint_in);
        } else if(paint_in.is_radial_gradient()) {
            color = new radial_gradient_solver(paint_in);
        } else if(paint_in.is_texture()) { 
            color = new texture_solver(paint_in);
        } else {
            RGBA8 s_transparent(0, 0, 0, 0);
            unorm8 s_opacity(0);
            paint s_paint(s_transparent, s_opacity);
            color = new color_solver(s_paint);
        }
    }
    scene_object(const scene_object &rhs) = delete;
    scene_object& operator=(const scene_object &rhs) = delete;
    inline ~scene_object() {
        for(auto &seg : m_path) {
            delete seg;
            seg = NULL;
        }
        delete color;
        m_path.clear();
    }
    inline bool hit(const double x, const double y) const {
        if(m_bbox.hit_inside(x, y)) { 
            int sum = 0;
            for(auto &seg : m_path){
                if(seg->intersect(x, y)) {
                    sum += seg->get_dir();
                }
            }
            if(m_wrule == e_winding_rule::non_zero){
                return (sum != 0);
            }
            else if(m_wrule == e_winding_rule::odd){
                return ((sum % 2)!= 0);
            }
        }
        return false;
    }
    inline RGBA8 get_color(const double x, const double y) const {
        return color->solve(x, y);
    }
};

class accelerated {
public:
    std::vector<scene_object*> objects;
    std::vector<R2> samples;
    int threads;
    inline accelerated()
        : samples{make_R2(0, 0)}
        , threads(1)
    {}
    inline accelerated(const accelerated& acc)
        : objects(acc.objects)
        , samples(acc.samples)
        , threads(acc.threads)
    {}
    accelerated& operator=(const accelerated& acc) {
        objects.clear();
        samples.clear();
        objects = acc.objects;
        samples = acc.samples;
        threads = acc.threads;
        return (*this);
    }
    inline ~accelerated() {
        objects.clear();
    }
    inline void destroy() { 
        for(auto &obj : objects) {
            delete obj;
            obj = NULL;
        }
    }
    inline void add(scene_object* obj){
        objects.push_back(obj);
    }
};

class monotonic_builder final: public i_input_path<monotonic_builder> {
friend i_input_path<monotonic_builder>;
    std::vector<path_segment*> m_path;
    R2 m_last_move;
public:
    inline monotonic_builder() 
        : m_last_move(make_R2(0, 0))
    {};
    inline ~monotonic_builder()
    {};
    inline void do_linear_segment(rvgf x0, rvgf y0, rvgf x1, rvgf y1){
        std::vector<R2> points{make_R2(x0, y0), make_R2(x1, y1)};
        m_path.push_back(new linear(points[0], points[1]));
    };
    inline void do_quadratic_segment(rvgf x0, rvgf y0, rvgf x1, rvgf y1,rvgf x2, rvgf y2){
        m_path.push_back(new quadratic(make_R2(x0, y0), make_R2(x1, y1), make_R2(x2, y2)));
    }
    inline void do_rational_quadratic_segment(rvgf x0, rvgf y0, rvgf x1, rvgf y1, rvgf w1, rvgf x2, rvgf y2){
        m_path.push_back(new quadratic(make_R2(x0, y0), make_R2(x1, y1), make_R2(x2, y2), w1));    
    };
    inline void do_cubic_segment(rvgf x0, rvgf y0, rvgf x1, rvgf y1, rvgf x2, rvgf y2, rvgf x3, rvgf y3){
        m_path.push_back(new cubic(make_R2(x0, y0), make_R2(x1, y1), make_R2(x2, y2), make_R2(x3, y3)));
    };
    inline void do_begin_contour(rvgf x0, rvgf y0){
        m_last_move = make_R2(x0, y0);
    };
    inline void do_end_open_contour(rvgf x0, rvgf y0){
        this->do_linear_segment(x0, y0, m_last_move[0], m_last_move[1]);
    };
    inline void do_end_closed_contour(rvgf x0, rvgf y0){
        (void) x0;
        (void) y0;
    };
    std::vector<path_segment*>& get() {
        return m_path;
    }
};
class accelerated_builder final: public i_scene_data<accelerated_builder> {
private:
    friend i_scene_data<accelerated_builder>;
    accelerated &acc;
    std::vector<xform> m_xf_stack;
    inline void pop_xf(){
        if (m_xf_stack.size() > 0) {
            m_xf_stack.pop_back();
        }
    }
    inline void push_xf(const xform &xf){
        m_xf_stack.push_back(top_xf() * xf);
    }
    inline const xform &top_xf() const{
        static xform id;
        if (m_xf_stack.empty()) return id;
        else return m_xf_stack.back();
    }
    inline void do_painted_shape(e_winding_rule wr, const shape &s, const paint &p){
        xform post;
        monotonic_builder path_builder;
        path_data::const_ptr path_data = s.as_path_data_ptr(post);
        const xform s_xf = post*top_xf()*s.get_xf();
        path_data->iterate(make_input_path_f_close_contours(
                           make_input_path_f_xform(s_xf,
                           make_input_path_f_downgrade_degenerate(
                           make_input_path_f_monotonize(
                           path_builder)))));
        if(path_builder.get().size() > 0) 
            acc.add(new scene_object(path_builder.get(), wr, p.transformed(top_xf())));
    }
    inline void do_begin_transform(uint16_t depth, const xform &xf){
        (void) depth;
        push_xf(xf);
    }
    inline void do_end_transform(uint16_t depth, const xform &xf){
        (void) depth;
        (void) xf;
        pop_xf(); 
    }
    inline void do_tensor_product_patch(const patch<16,4> &tpp){(void) tpp;};
    inline void do_coons_patch(const patch<12,4> &cp){(void) cp;};
    inline void do_gouraud_triangle(const patch<3,3> &gt){(void) gt;};
    inline void do_stencil_shape(e_winding_rule wr, const shape &s){(void) wr;(void) s;};
    inline void do_begin_clip(uint16_t depth){(void) depth;};
    inline void do_activate_clip(uint16_t depth){(void) depth;};
    inline void do_end_clip(uint16_t depth){(void) depth;};
    inline void do_begin_fade(uint16_t depth, unorm8 opacity){(void) depth;(void) opacity;};
    inline void do_end_fade(uint16_t depth, unorm8 opacity){(void) depth;(void) opacity;};
    inline void do_begin_blur(uint16_t depth, float radius){(void) depth;(void) radius;};
    inline void do_end_blur(uint16_t depth, float radius){(void) depth;(void) radius;};

public:
    inline accelerated_builder(accelerated &acc_in, const std::vector<std::string> &args, 
        const xform &screen_xf)
        :   acc(acc_in) {
        unpack_args(args);
        push_xf(screen_xf);
    }
    inline void unpack_args(const std::vector<std::string> &args) {
        acc.samples = blue_1;
        double tx = 0;
        double ty = 0;
        for (auto &arg : args) {
            std::string delimiter = ":";
            std::string command = arg.substr(0, arg.find(delimiter)); 
            std::string value = arg.substr(arg.find(delimiter)+1, arg.length()); 
            if(command == std::string{"-pattern"}) {
                if(value == std::string{"1"}) {
                    acc.samples = blue_1;
                } else if(value == std::string{"8"}) {
                    acc.samples = blue_8;
                } else if(value == std::string{"16"}) {
                    acc.samples = blue_16;
                } else if(value == std::string{"32"}) {
                    acc.samples = blue_32;
                } else if(value == std::string{"64"}) {
                    acc.samples = blue_64;
                }
            } else if(command == std::string{"-tx"}) {
                tx = std::stof(value);
            } else if(command == std::string{"-ty"}) {
                ty = std::stof(value);
            } else if(command == std::string{"-j"}) {
                acc.threads = std::stoi(value);
            }
        }
        push_xf(translation(tx, ty));
    }
};

const accelerated accelerate(const scene &c, const window &w,
    const viewport &v, const std::vector<std::string> &args) {
    accelerated acc;
    accelerated_builder builder(acc, args, make_windowviewport(w, v) * c.get_xf());
    c.get_scene_data().iterate(builder);
    return acc;
}

RGBA8 sample(const accelerated& a, float x, float y){
    RGBA8 c = make_rgba8(0, 0, 0, 0);
    for(auto obj_it = a.objects.rbegin(); obj_it != a.objects.rend(); ++obj_it) {
        auto obj = (*obj_it);
        if(obj->hit(x, y)){
            c = over(c, pre_multiply(obj->get_color(x, y)));
            if((int) c[3] == 255) {
                return c;
            }
        }
    }
    return over(c, make_rgba8(255, 255, 255, 255));
}

void render(accelerated &a, const window &w, const viewport &v,
    FILE *out, const std::vector<std::string> &args) {
    (void) args;
    (void) w;
    int xl, yb, xr, yt;
    std::tie(xl, yb) = v.bl();
    std::tie(xr, yt) = v.tr();
    int width = xr - xl;
    int height = yt - yb;
    image<uint8_t, 4> out_image;
    out_image.resize(width, height);
    #pragma omp parallel for num_threads(a.threads)
    for (int i = 1; i <= height; i++) {
        for (int j = 1; j <= width; j++) {
            std::vector<int> color{0, 0, 0, 255};
            for(auto &sp : a.samples) {
                double y = yb+i-0.5+sp[0];
                double x = xl+j-0.5+sp[1];
                RGBA8 sp_color(remove_gamma(sample(a, x, y)));
                color[0] += (int)sp_color[0];
                color[1] += (int)sp_color[1];
                color[2] += (int)sp_color[2];
            }
            color[0] /= a.samples.size();
            color[1] /= a.samples.size();
            color[2] /= a.samples.size();
            RGBA8 g_color = add_gamma(make_rgba8(color[0], color[1], color[2], color[3]));
            out_image.set_pixel(j-1, i-1, g_color[0], g_color[1], g_color[2], 255);
        }
    }
    store_png<uint8_t>(out, out_image);
    a.destroy();
}

} } } // namespace rvg::driver::png

// Lua version of the accelerate function.
// Since there is no acceleration, we simply
// and return the input scene unmodified.
static int luaaccelerate(lua_State *L) {
    rvg_lua_push<rvg::driver::png::accelerated>(L,
        rvg::driver::png::accelerate(
            rvg_lua_check<rvg::scene>(L, 1),
            rvg_lua_check<rvg::window>(L, 2),
            rvg_lua_check<rvg::viewport>(L, 3),
            rvg_lua_optargs(L, 4)));
    return 1;
}

// Lua version of render function
static int luarender(lua_State *L) {
    auto a = rvg_lua_check<rvg::driver::png::accelerated>(L, 1);
    auto w = rvg_lua_check<rvg::window>(L, 2);
    auto v = rvg_lua_check<rvg::viewport>(L, 3);
    auto o = rvg_lua_optargs(L, 5);
    rvg::driver::png::render(a, w, v, rvg_lua_check_file(L, 4), o);
    return 0;
}

// List of Lua functions exported into driver table
static const luaL_Reg modpngpng[] = {
    {"render", luarender },
    {"accelerate", luaaccelerate },
    {NULL, NULL}
};

// Lua function invoked to be invoked by require"driver.png"
extern "C"
#ifndef _WIN32
__attribute__((visibility("default")))
#else
__declspec(dllexport)
#endif
int luaopen_driver_png(lua_State *L) {
    rvg_lua_init(L);
    if (!rvg_lua_typeexists<rvg::driver::png::accelerated>(L, -1)) {
        rvg_lua_createtype<rvg::driver::png::accelerated>(L,
            "png accelerated", -1);
    }
    rvg_lua_facade_new_driver(L, modpngpng);
    return 1;
}
