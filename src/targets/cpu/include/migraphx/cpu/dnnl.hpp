#ifndef MIGRAPHX_GUARD_AMDMIGRAPHX_DNNL_HPP
#define MIGRAPHX_GUARD_AMDMIGRAPHX_DNNL_HPP

#include <migraphx/config.hpp>
#include <migraphx/argument.hpp>
#include <migraphx/reflect.hpp>
#include <migraphx/register_op.hpp>
#include <migraphx/check_shapes.hpp>
#include <unordered_map>
#include <dnnl.hpp>
#include <migraphx/errors.hpp>

namespace migraphx {
inline namespace MIGRAPHX_INLINE_NS {
namespace cpu {

struct dnnl_context
{
    dnnl::engine engine;
    dnnl::stream stream;
    dnnl_context() : engine(dnnl::engine::kind::cpu, 0), stream(engine) {}
};

dnnl_context& get_dnnl_context();

dnnl::memory::data_type to_dnnl_memory_data_type(shape::type_t t);

dnnl::memory::format_tag to_dnnl_memory_format_tag(std::size_t n);

template <class R>
inline dnnl::memory::dims to_dnnl_dims(R&& r)
{
    return {r.begin(), r.end()};
}

dnnl::memory::desc to_dnnl_memory_desc(const shape& s);

dnnl::memory to_dnnl_memory(const dnnl::memory::desc& desc, const argument& a);

dnnl::memory to_dnnl_memory(const argument& a);

dnnl::algorithm to_dnnl_algo(const std::string& name);

struct post_op: reflect_equality<post_op>, reflect_stream<post_op>
{
    std::string name;
    float alpha = 0;
    float beta  = 0;
    template <class Self, class F>
    static auto reflect(Self& self, F f)
    {
        return pack(f(self.name, "name"), f(self.alpha, "alpha"), f(self.beta, "beta"));
    }
};

template <class Derived, class Primitive>
struct dnnl_op : auto_register_op<Derived>
{
    std::vector<post_op> post_ops;
    std::function<argument(context& ctx, const std::vector<argument>& args)> execute;

    template <class Self, class F>
    static auto reflect_base(Self& self, F f)
    {
        return pack(f(self.post_ops, "post_ops"));
    }

    template <class Self, class F>
    static auto reflect(Self& self, F f)
    {
        return reflect_base(self, f);
    }

    std::size_t get_extra_post_op_args() const
    {
        return std::count_if(post_ops.begin(), post_ops.end(), [](const auto& po) {
            return contains(po.name, "binary");
        });
    }

    static std::size_t get_binary_post_op_arg(std::size_t pos)
    {
        return DNNL_ARG_ATTR_MULTIPLE_POST_OP(pos) | DNNL_ARG_SRC_1;
    }

    static std::vector<shape> to_shapes(const std::vector<argument>& args)
    {
        std::vector<shape> shapes(args.size());
        std::transform(args.begin(), args.end(), shapes.begin(), [](const argument& a) {
            return a.get_shape();
        });
        return shapes;
    }
    // Map arg index to arg in dnnl
    std::vector<int> arg_map(int size) const
    {
        std::vector<int> result(size);
        std::iota(result.begin(), result.end(), DNNL_ARG_SRC_0);
        return result;
    }
    shape base_adjust_shape(const shape& s) const
    {
        if(s.broadcasted())
        {
            auto lens    = s.lens();
            auto strides = s.strides();
            std::transform(strides.begin(),
                           strides.end(),
                           lens.begin(),
                           lens.begin(),
                           [](auto stride, auto len) -> std::size_t {
                               if(stride == 0)
                                   return 1;
                               else
                                   return len;
                           });
            return shape{s.type(), lens};
        }
        return s;
    }
    shape adjust_shape(const shape& s, int) const { return base_adjust_shape(s); }
    std::vector<int> create_arg_map(std::size_t input_size) const
    {
        const auto& self     = static_cast<const Derived&>(*this);
        auto npost_ops       = get_extra_post_op_args();
        auto prim_input_size = input_size - npost_ops;
        auto m               = self.arg_map(prim_input_size);
        for(std::size_t i = 0; i < npost_ops; i++)
        {
            m.push_back(get_binary_post_op_arg(i));
        }
        return m;
    }
    std::unordered_map<int, dnnl::memory::desc>
    to_memory_desc(const shape& output_shape, const std::vector<shape>& inputs) const
    {
        const auto& self = static_cast<const Derived&>(*this);
        std::unordered_map<int, dnnl::memory::desc> result;
        result[DNNL_ARG_DST] = to_dnnl_memory_desc(self.adjust_shape(output_shape, inputs.size()));
        auto m               = create_arg_map(inputs.size());
        assert(m.size() >= inputs.size());
        for(int i = 0; i < inputs.size(); i++)
        {
            result[m[i]] = to_dnnl_memory_desc(self.adjust_shape(inputs[i], i));
        }
        return result;
    }
    dnnl::primitive_attr
    get_primitive_attr(const std::unordered_map<int, dnnl::memory::desc>& m) const
    {
        dnnl::primitive_attr result;
        dnnl::post_ops po;
        int binary_post_op_pos = 1;
        for(auto&& op : post_ops)
        {
            if(contains(op.name, "binary"))
            {
                po.append_binary(to_dnnl_algo(op.name),
                                 m.at(get_binary_post_op_arg(binary_post_op_pos)));
                binary_post_op_pos++;
            }
            if(contains(op.name, "eltwise"))
                po.append_eltwise(1.0f, to_dnnl_algo(op.name), op.alpha, op.beta);
        }
        result.set_post_ops(po);
        return result;
    }
    template <class T>
    auto get_primitive_desc(const T& desc, const dnnl::primitive_attr& attr) const
        -> decltype(typename Primitive::primitive_desc(desc, attr, get_dnnl_context().engine))
    {
        return typename Primitive::primitive_desc(desc, attr, get_dnnl_context().engine);
    }
    Primitive get_primitive(const std::unordered_map<int, dnnl::memory::desc>& m) const
    {
        const auto& self = static_cast<const Derived&>(*this);
        auto desc        = self.get_desc(m);
        auto attr        = get_primitive_attr(m);
        auto pd          = self.get_primitive_desc(desc, attr);
        return Primitive(pd);
    }
    argument compute(context& ctx, const shape&, const std::vector<argument>& args) const
    {
        return execute(ctx, args);
    }

    std::ptrdiff_t output_alias(const std::vector<shape>& shapes) const
    {
        return shapes.size() - 1;
    }

    void finalize(context&, const shape& output_shape, std::vector<shape> inputs)
    {
        // Compensate for allocation
        inputs.pop_back();
        const auto& self = static_cast<const Derived&>(*this);
        auto name        = self.name();
        auto md          = to_memory_desc(output_shape, inputs);
        auto prim        = get_primitive(md);
        auto arg_lookup  = create_arg_map(inputs.size());
        execute          = [=](context&, const std::vector<argument>& args) {
#ifndef NDEBUG
            // Check that the memory descriptors have not changed
            auto debug_args = args;
            debug_args.pop_back();
            auto debug_md = to_memory_desc(output_shape, to_shapes(debug_args));
            for(auto&& p : debug_md)
            {
                if(md.count(p.first) == 0)
                    MIGRAPHX_THROW(name +
                                   ": Missing memory descriptor for: " + std::to_string(p.first));
                if(p.second == md.at(p.first))
                    continue;
                MIGRAPHX_THROW(name +
                               ": Memory descriptor has changed for: " + std::to_string(p.first));
            }
#endif
            std::unordered_map<int, dnnl::memory> m;
            m[DNNL_ARG_DST] = to_dnnl_memory(md.at(DNNL_ARG_DST), args.back());
            for(int i = 0; i < args.size() - 1; i++)
                m[arg_lookup[i]] = to_dnnl_memory(md.at(arg_lookup[i]), args[i]);
            prim.execute(get_dnnl_context().stream, m);
            return args.back();
        };
    }
    std::vector<shape> trim_post_op_inputs(const std::vector<shape>& inputs) const
    {
        auto prim_input_size = inputs.size() - this->get_extra_post_op_args();
        return {inputs.begin(), inputs.begin() + prim_input_size};
    }
};

template <class Derived, class Primitive, class Op>
struct dnnl_extend_op : dnnl_op<Derived, Primitive>
{
    Op op;

    template <class Self, class F>
    static auto reflect(Self& self, F f)
    {
        return pack_join(self.reflect_base(self, f), migraphx::reflect(self.op, f));
    }

    // dnnl has some issues with non-packed inputs
    void required(const check_shapes& cs) const { cs.packed_or_broadcasted(); }

    std::string name() const { return "dnnl::" + op.name(); }
    shape compute_shape(std::vector<shape> inputs) const
    {
        const auto& self = static_cast<const Derived&>(*this);
        // Compensate for allocation
        inputs.pop_back();
        self.required(check_shapes(inputs, self));
        auto r = migraphx::compute_shape(op, this->trim_post_op_inputs(inputs));
        // Call to get_primitive to make sure an algo is available
        this->get_primitive(this->to_memory_desc(r, inputs));
        return r;
    }
};

} // namespace cpu
} // namespace MIGRAPHX_INLINE_NS
} // namespace migraphx

#endif
