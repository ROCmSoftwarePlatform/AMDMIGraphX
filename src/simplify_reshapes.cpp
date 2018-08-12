#include <migraph/simplify_reshapes.hpp>
#include <migraph/program.hpp>
#include <migraph/instruction.hpp>
#include <migraph/operators.hpp>
#include <migraph/iterator_for.hpp>
#include <migraph/ranges.hpp>
#include <unordered_set>

namespace migraph {

bool is_reshaper(const std::string& name)
{
    static const std::unordered_set<std::string> names = {"reshape",
                                                          "transpose",
                                                          // "broadcast",
                                                          "contiguous"};
    return contains(names, name);
}

void simplify_reshapes::apply(program& p) const
{
    for(auto ins : iterator_for(p))
    {
        if(not is_reshaper(ins->op.name()))
            continue;
        if(ins->output.size() != 1)
            continue;
        if(is_reshaper(ins->output.front()->op.name()))
            continue;
        // Gather reshapes
        std::vector<instruction_ref> reshapes{ins};
        while(is_reshaper(reshapes.back()->op.name()))
        {
            assert(!reshapes.back()->arguments.empty());
            assert(p.has_instruction(reshapes.back()->arguments.front()));
            reshapes.push_back(reshapes.back()->arguments.front());
        }

        std::pair<instruction_ref, instruction_ref> r{p.end(), p.end()};
        for(auto start : iterator_for(reshapes))
        {
            auto last = std::find_if(reshapes.rbegin(), reshapes.rend(), [&](auto&& i) {
                return i->result == (*start)->result and i != (*start);
            });
            if(last != reshapes.rend())
            {
                r = std::make_pair(*start, *last);
                break;
            }
        }
        if(r.first != r.second)
        {
            p.replace_instruction(r.first, r.second);
        }
    }
}

} // namespace migraph
