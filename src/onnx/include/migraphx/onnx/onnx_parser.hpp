#ifndef MIGRAPHX_GUARD_AMDMIGRAPHX_ONNX_PARSER_HPP
#define MIGRAPHX_GUARD_AMDMIGRAPHX_ONNX_PARSER_HPP

#include <migraphx/config.hpp>
#include <migraphx/program.hpp>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <onnx.pb.h>
#include <unordered_map>
#include <functional>
#include <utility>
#include <vector>

namespace migraphx {
inline namespace MIGRAPHX_INLINE_NS {
namespace onnx {

namespace onnx = onnx_for_migraphx;

struct onnx_parser
{
    std::string filename;
    std::string path    = ".";
    using attribute_map = std::unordered_map<std::string, onnx::AttributeProto>;
    struct node_info
    {
        attribute_map attributes{};
        std::size_t num_outputs = 1;
        std::string name        = "";
        module* mm              = nullptr;
        instruction_ref make_contiguous(instruction_ref ins) const;
        instruction_ref add_broadcastable_binary_op(const std::string& name,
                                                    instruction_ref arg0,
                                                    instruction_ref arg1) const;
        instruction_ref add_instruction(const operation& op,
                                        const std::vector<instruction_ref>& args) const;

        template <class... Ts>
        instruction_ref add_instruction(const operation& op, Ts... xs) const
        {
            return add_instruction(op, {xs...});
        }
    };
    using node_map = std::unordered_map<std::string, onnx::NodeProto>;
    using op_func  = std::function<std::vector<instruction_ref>(
        const onnx_parser&, const node_info&, std::vector<instruction_ref>)>;
    node_map nodes;
    std::unordered_map<std::string, instruction_ref> instructions;
    program prog                  = program();
    bool is_pytorch               = false;
    std::size_t default_dim_value = 1;
    std::unordered_map<std::string, std::vector<std::size_t>> map_input_dims;
    bool skip_unknown_operators = false;

    std::unordered_map<std::string, op_func> ops;
    std::unordered_map<std::string, operation> map_actv_funcs;

    onnx_parser();
    operation load(const std::string& name, const node_info& info) const;

    template <class F>
    void add_op_parser(std::string name, F f)
    {
        ops.emplace(name, [=](auto&&... xs) {
            return std::vector<instruction_ref>{f(std::forward<decltype(xs)>(xs)...)};
        });
    }

    // Multi output op
    template <class F>
    void add_multi_op_parser(std::string name, F f)
    {
        ops.emplace(name, f);
    }

    void add_binary_op_parser(const std::string& onnx_name, const std::string& op_name);
    void add_generic_op_parser(const std::string& onnx_name,
                               const std::string& op_name,
                               bool contiguous = false);
    void add_variadic_op_parser(const std::string& onnx_name, const std::string& op_name);

    void parse_undefined(module* mm, const std::string& name);

    void parse_from(std::istream& is, std::string name = "");
    void parse_from(const void* data, std::size_t size);
    void parse_graph(const onnx::GraphProto& graph);
    literal parse_value(const onnx::AttributeProto& attr) const;
    literal parse_tensor(const onnx::TensorProto& t) const;
    shape parse_type(const onnx::TypeProto& t, const std::vector<std::size_t>& input_dims) const;
};

shape::type_t get_type(int dtype);

} // namespace onnx
} // namespace MIGRAPHX_INLINE_NS
} // namespace migraphx

#endif
