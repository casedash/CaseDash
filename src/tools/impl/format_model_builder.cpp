#include "tools/impl/format_model_builder.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tools/impl/tools_common.h"

namespace {

std::string_view NodeText(TSNode node, std::string_view source) {
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (start > end || end > source.size()) {
        return {};
    }
    return source.substr(start, end - start);
}

bool ContainsBlankLine(std::string_view source, uint32_t firstEnd, uint32_t secondStart) {
    if (firstEnd >= secondStart || secondStart > source.size()) {
        return false;
    }
    int lineBreaks = 0;
    bool sawNonWhitespace = false;
    for (size_t index = firstEnd; index < secondStart; ++index) {
        const char ch = source[index];
        if (ch == '\r' || ch == '\n') {
            ++lineBreaks;
            if (ch == '\r' && index + 1 < secondStart && source[index + 1] == '\n') {
                ++index;
            }
            if (lineBreaks >= 2 && !sawNonWhitespace) {
                return true;
            }
            continue;
        }
        if (ch != ' ' && ch != '\t' && ch != '\v' && ch != '\f') {
            sawNonWhitespace = true;
        }
    }
    return lineBreaks >= 2 && !sawNonWhitespace;
}

std::string_view TrimLeadingWhitespace(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    return value;
}

SyntaxNode* MakeNode(FormatModel& model) {
    return &model.nodes.emplace_back(model.childStorage.get());
}

void SetParentRecursive(SyntaxNode& node, const SyntaxNode* parent) {
    node.parent = parent;
    node.depth = parent == nullptr ? 0 : parent->depth + 1;
    for (SyntaxNode* child : node.children) {
        if (child != nullptr) {
            SetParentRecursive(*child, &node);
        }
    }
}

void AppendChild(SyntaxNode& parent, SyntaxNode* child) {
    if (child != nullptr) {
        SetParentRecursive(*child, &parent);
    }
    parent.children.push_back(child);
}

SyntaxNode* MakeBlankLine(FormatModel& model) {
    SyntaxNode* node = MakeNode(model);
    node->kind = SyntaxNodeKind::BlankLine;
    return node;
}

SyntaxNode* MakeTokenNode(FormatModel& model, SyntaxNodeKind token) {
    SyntaxNode* node = MakeNode(model);
    node->kind = token;
    return node;
}

void SetKnownTokenNode(SyntaxNode& node, SyntaxNodeKind token, std::string_view text) {
    node.kind = token;
    if (text != SyntaxNodeKindTokenText(token)) {
        node.text = text;
    }
}

bool MacroLikeInvocationNode(const SyntaxNode* node) {
    return node != nullptr &&
        node->kind == SyntaxNodeKind::Tree &&
        node->children.size() == 2 &&
        node->children[0] != nullptr &&
        node->children[0]->kind == SyntaxNodeKind::Identifier &&
        node->children[1] != nullptr &&
        node->children[1]->kind == SyntaxNodeKind::ArgumentList;
}

bool MacroLikeInvocationEnding(const SyntaxChildList& children, size_t index) {
    if (MacroLikeInvocationNode(children[index])) {
        return true;
    }
    return index > 0 &&
        children[index] != nullptr &&
        children[index]->kind == SyntaxNodeKind::ArgumentList &&
        children[index - 1] != nullptr &&
        children[index - 1]->kind == SyntaxNodeKind::Identifier;
}

bool ContainsWholeAtomDelimiter(std::string_view text) {
    // Whole-atom wrappers are safe to keep as one text node only when the text has no structural separators.
    // Examples that take the shortcut: `name`, `ns::Type`, `*ptr`, `++index`.
    // Examples that must still expose children: `call(arg)`, `array[i]`, `T<U>`, `x + y`.
    for (const char ch : text) {
        switch (ch) {
            case '\t':
            case '\n':
            case '\r':
            case ' ':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
            case '<':
            case '>':
            case ',':
            case ';':
            case '"':
            case '\'':
                return true;
            default:
                break;
        }
    }
    return false;
}

bool ContainsWholeFieldAtomDelimiter(std::string_view text) {
    // Field expressions get the same shortcut, but `object->field` is still atom-like; the `>` in `->` is allowed.
    for (size_t index = 0; index < text.size(); ++index) {
        switch (text[index]) {
            case '\t':
            case '\n':
            case '\r':
            case ' ':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
            case '<':
            case ',':
            case ';':
            case '"':
            case '\'':
                return true;
            case '>':
                if (index == 0 || text[index - 1] != '-') {
                    return true;
                }
                break;
            default:
                break;
        }
    }
    return false;
}

bool CompactEmptyDelimitedText(std::string_view text) {
    return text == "()" || text == "[]" || text == "<>" || text == "{}";
}

std::optional<size_t> PreviousNonTriviaChildIndex(const SyntaxChildList& children, size_t before) {
    while (before > 0) {
        --before;
        const SyntaxNode* child = children[before];
        if (child == nullptr || !SyntaxNodeKindHasClass(child->kind, TokenClass::Trivia)) {
            return before;
        }
    }
    return std::nullopt;
}

std::optional<size_t> NextNonTriviaChildIndex(const SyntaxChildList& children, size_t after) {
    for (size_t index = after; index < children.size(); ++index) {
        const SyntaxNode* child = children[index];
        if (child == nullptr || !SyntaxNodeKindHasClass(child->kind, TokenClass::Trivia)) {
            return index;
        }
    }
    return std::nullopt;
}

void NormalizeTrailingCommas(FormatModel& model, SyntaxNode& node) {
    SyntaxChildList& children = node.children;
    for (size_t index = 0; index < children.size(); ++index) {
        if (children[index] == nullptr) {
            continue;
        }
        if (
            children[index]->kind != SyntaxNodeKind::RightBrace &&
            children[index]->kind != SyntaxNodeKind::RightParen &&
            children[index]->kind != SyntaxNodeKind::RightBracket &&
            children[index]->kind != SyntaxNodeKind::Greater
        ) {
            continue;
        }
        const std::optional<size_t> previous = PreviousNonTriviaChildIndex(children, index);
        if (!previous) {
            continue;
        }
        if (node.kind == SyntaxNodeKind::EnumeratorList) {
            if (
                children[*previous]->kind != SyntaxNodeKind::Comma &&
                children[*previous]->kind != SyntaxNodeKind::LeftBrace &&
                !MacroLikeInvocationEnding(children, *previous)
            ) {
                SyntaxNode* comma = MakeTokenNode(model, SyntaxNodeKind::Comma);
                comma->parent = &node;
                comma->depth = node.depth + 1;
                children.insert(children.begin() + static_cast<std::ptrdiff_t>(*previous + 1), comma);
                ++index;
            }
            continue;
        }
        if (children[*previous]->kind == SyntaxNodeKind::Comma) {
            children.erase(children.begin() + static_cast<std::ptrdiff_t>(*previous));
            --index;
        }
    }
}

void WrapControlBody(FormatModel& model, SyntaxNode& node, size_t childIndex) {
    if (childIndex >= node.children.size() || (
        node.children[childIndex] != nullptr && node.children[childIndex]->kind == SyntaxNodeKind::CompoundStatement
    )) {
        return;
    }
    size_t firstBodyIndex = childIndex;
    while (firstBodyIndex > 0 && node.children[firstBodyIndex - 1] != nullptr && SyntaxNodeKindHasClass(
        node.children[firstBodyIndex - 1]->kind,
        TokenClass::Comment
    )) {
        --firstBodyIndex;
    }

    SyntaxNode* compound = MakeNode(model);
    compound->kind = SyntaxNodeKind::CompoundStatement;
    compound->parent = &node;
    compound->depth = node.depth + 1;
    compound->children.reserve(childIndex - firstBodyIndex + 3);
    AppendChild(*compound, MakeTokenNode(model, SyntaxNodeKind::LeftBrace));
    for (size_t index = firstBodyIndex; index <= childIndex; ++index) {
        AppendChild(*compound, node.children[index]);
    }
    AppendChild(*compound, MakeTokenNode(model, SyntaxNodeKind::RightBrace));

    node.children.erase(
        node.children.begin() + static_cast<std::ptrdiff_t>(firstBodyIndex),
        node.children.begin() + static_cast<std::ptrdiff_t>(childIndex + 1)
    );
    node.children.insert(node.children.begin() + static_cast<std::ptrdiff_t>(firstBodyIndex), compound);
}

std::optional<size_t> FindOnlyIfInBraceBlock(const SyntaxNode& node) {
    if (node.kind != SyntaxNodeKind::CompoundStatement) {
        return std::nullopt;
    }
    std::optional<size_t> ifIndex;
    for (size_t index = 0; index < node.children.size(); ++index) {
        const SyntaxNode* child = node.children[index];
        if (child == nullptr) {
            return std::nullopt;
        }
        if (child->kind == SyntaxNodeKind::LeftBrace || child->kind == SyntaxNodeKind::RightBrace) {
            continue;
        }
        if (child->kind == SyntaxNodeKind::IfStatement && !ifIndex) {
            ifIndex = index;
            continue;
        }
        return std::nullopt;
    }
    return ifIndex;
}

void NormalizeElseClauseBody(FormatModel& model, SyntaxNode& node) {
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (node.children[index] == nullptr || node.children[index]->kind != SyntaxNodeKind::KeywordElse) {
            continue;
        }
        const std::optional<size_t> bodyIndex = NextNonTriviaChildIndex(node.children, index + 1);
        if (!bodyIndex) {
            return;
        }
        if (node.children[*bodyIndex] != nullptr && node.children[*bodyIndex]->kind == SyntaxNodeKind::IfStatement) {
            return;
        }
        if (
            node.children[*bodyIndex] != nullptr && node.children[*bodyIndex]->kind == SyntaxNodeKind::CompoundStatement
        ) {
            std::optional<size_t> ifIndex = FindOnlyIfInBraceBlock(*node.children[*bodyIndex]);
            if (ifIndex) {
                node.children[*bodyIndex] = node.children[*bodyIndex]->children[*ifIndex];
                SetParentRecursive(*node.children[*bodyIndex], &node);
                return;
            }
            return;
        }
        WrapControlBody(model, node, *bodyIndex);
        return;
    }
}

void NormalizeIfStatementBody(FormatModel& model, SyntaxNode& node) {
    size_t before = node.children.size();
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (node.children[index] != nullptr && node.children[index]->kind == SyntaxNodeKind::ElseClause) {
            before = index;
            break;
        }
    }
    const std::optional<size_t> consequenceIndex = PreviousNonTriviaChildIndex(node.children, before);
    if (consequenceIndex) {
        WrapControlBody(model, node, *consequenceIndex);
    }
}

void NormalizeDoStatementBody(FormatModel& model, SyntaxNode& node) {
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (node.children[index] == nullptr || node.children[index]->kind != SyntaxNodeKind::KeywordWhile) {
            continue;
        }
        const std::optional<size_t> bodyIndex = PreviousNonTriviaChildIndex(node.children, index);
        if (bodyIndex) {
            WrapControlBody(model, node, *bodyIndex);
        }
        return;
    }
}

void NormalizeLastControlBody(FormatModel& model, SyntaxNode& node) {
    const std::optional<size_t> bodyIndex = PreviousNonTriviaChildIndex(node.children, node.children.size());
    if (bodyIndex) {
        WrapControlBody(model, node, *bodyIndex);
    }
}

void NormalizeControlBodies(FormatModel& model, SyntaxNode& node) {
    switch (node.kind) {
        case SyntaxNodeKind::IfStatement:
            NormalizeIfStatementBody(model, node);
            return;
        case SyntaxNodeKind::ElseClause:
            NormalizeElseClauseBody(model, node);
            return;
        case SyntaxNodeKind::ForStatement:
        case SyntaxNodeKind::WhileStatement:
        case SyntaxNodeKind::SwitchStatement:
            NormalizeLastControlBody(model, node);
            return;
        case SyntaxNodeKind::DoStatement:
            NormalizeDoStatementBody(model, node);
            return;
        default:
            return;
    }
}

void NormalizeSyntaxNode(FormatModel& model, SyntaxNode& node) {
    NormalizeTrailingCommas(model, node);
    NormalizeControlBodies(model, node);
}

struct TsNodeSyntax {
    TSSymbol symbol = 0;
    SyntaxNodeKind kind = SyntaxNodeKind::Unknown;
    SyntaxNodeKind tokenKind = SyntaxNodeKind::Unknown;
    std::uint64_t classes = 0;
    SyntaxWrapperRole wrapperRole = SyntaxWrapperRole::None;
};

inline TsNodeSyntax GetTsNodeSyntax(TSNode tsNode) {
    const TSSymbol symbol = ts_node_symbol(tsNode);
    const SyntaxSymbolInfo info = SyntaxSymbolInfoForSymbol(symbol);
    return {
        .symbol = symbol,
        .kind = info.treeKind,
        .tokenKind = info.tokenKind,
        .classes = info.classes,
        .wrapperRole = info.wrapperRole
    };
}

bool TsNodeSyntaxHasClass(TsNodeSyntax syntax, TokenClass tokenClass) {
    return (syntax.classes & static_cast<std::uint64_t>(tokenClass)) != 0;
}

void AppendTsChildren(
    FormatModel& model,
    TSNode tsNode,
    std::string_view source,
    SyntaxNode& parent,
    uint32_t childCount
);

SyntaxNode* BuildNode(
    FormatModel& model,
    TSNode tsNode,
    std::string_view source,
    const SyntaxNode* parent,
    TsNodeSyntax syntax
) {
    SyntaxNode* node = MakeNode(model);
    node->parent = parent;
    node->depth = parent == nullptr ? 0 : parent->depth + 1;
    node->classes = syntax.classes;

    if (syntax.kind == SyntaxNodeKind::Comment) {
        node->kind = SyntaxNodeKind::Comment;
        std::string_view commentText = NodeText(tsNode, source);
        while (!commentText.empty() && (commentText.back() == '\r' || commentText.back() == '\n')) {
            commentText.remove_suffix(1);
        }
        node->text = commentText;
        return node;
    }

    if (syntax.wrapperRole == SyntaxWrapperRole::CompactEmptyDelimited) {
        const std::string_view text = NodeText(tsNode, source);
        if (CompactEmptyDelimitedText(text)) {
            node->kind = syntax.kind == SyntaxNodeKind::Unknown ? SyntaxNodeKind::FreeToken : syntax.kind;
            node->text = text;
            return node;
        }
    }
    if (syntax.wrapperRole == SyntaxWrapperRole::WholeToken) {
        const std::string_view text = NodeText(tsNode, source);
        const SyntaxNodeKind known = SyntaxNodeKindFromTokenText(text);
        if (known != SyntaxNodeKind::Unknown) {
            SetKnownTokenNode(*node, known, text);
            return node;
        }
    } else if (
        syntax.wrapperRole == SyntaxWrapperRole::WholeAtom || syntax.wrapperRole == SyntaxWrapperRole::WholeFieldAtom
    ) {
        const std::string_view text = NodeText(tsNode, source);
        if ((syntax.wrapperRole == SyntaxWrapperRole::WholeAtom && !ContainsWholeAtomDelimiter(text)) || (
            syntax.wrapperRole == SyntaxWrapperRole::WholeFieldAtom && !ContainsWholeFieldAtomDelimiter(text)
        )) {
            node->kind = syntax.kind == SyntaxNodeKind::Unknown ? SyntaxNodeKind::FreeToken : syntax.kind;
            node->text = text;
            return node;
        }
    }
    if (
        TsNodeSyntaxHasClass(syntax, TokenClass::WholeNodeAsFreeToken) ||
        TsNodeSyntaxHasClass(syntax, TokenClass::AtomicPreprocessor)
    ) {
        node->kind = syntax.kind;
        node->text = NodeText(tsNode, source);
        return node;
    }

    const uint32_t childCount = ts_node_child_count(tsNode);
    if (syntax.tokenKind != SyntaxNodeKind::Unknown && childCount == 0) {
        SetKnownTokenNode(*node, syntax.tokenKind, NodeText(tsNode, source));
        return node;
    }

    if (childCount == 0) {
        const std::string_view text = NodeText(tsNode, source);
        const SyntaxNodeKind knownFromText = SyntaxNodeKindFromTokenText(text);
        if (knownFromText != SyntaxNodeKind::Unknown) {
            SetKnownTokenNode(*node, knownFromText, text);
            return node;
        }
        node->kind = syntax.kind == SyntaxNodeKind::Unknown ? SyntaxNodeKind::FreeToken : syntax.kind;
        node->text = text;
        return node;
    }

    node->kind = syntax.kind == SyntaxNodeKind::Unknown ? SyntaxNodeKind::Tree : syntax.kind;
    if (
        node->kind == SyntaxNodeKind::PreprocIf ||
        node->kind == SyntaxNodeKind::PreprocIfdef ||
        node->kind == SyntaxNodeKind::PreprocElse ||
        node->kind == SyntaxNodeKind::PreprocElif
    ) {
        node->text = NodeText(tsNode, source);
    }
    node->children.reserve(childCount);
    AppendTsChildren(model, tsNode, source, *node, childCount);
    NormalizeSyntaxNode(model, *node);
    return node;
}

inline void AppendTsNode(
    FormatModel& model,
    TSNode tsNode,
    std::string_view source,
    SyntaxNode& parent,
    TsNodeSyntax syntax,
    bool isTrailingComment
) {
    if (syntax.wrapperRole == SyntaxWrapperRole::Flatten) {
        AppendTsChildren(model, tsNode, source, parent, ts_node_child_count(tsNode));
        return;
    }
    SyntaxNode* childNode = BuildNode(model, tsNode, source, &parent, syntax);
    if (isTrailingComment && childNode->kind == SyntaxNodeKind::Comment) {
        childNode->kind = SyntaxNodeKind::TrailingComment;
    }
    parent.children.push_back(childNode);
}

inline void AppendTsChild(
    FormatModel& model,
    TSNode child,
    uint32_t childEnd,
    uint32_t childEndRow,
    uint32_t childEndColumn,
    std::string_view source,
    SyntaxNode& parent,
    uint32_t& previousEnd,
    uint32_t& previousEndRow,
    uint32_t& previousEndColumn,
    bool& hasPreviousSibling
) {
    const TsNodeSyntax childSyntax = GetTsNodeSyntax(child);
    const uint32_t childStart = ts_node_start_byte(child);
    const uint32_t childStartRow = ts_node_start_point(child).row;
    if (
        hasPreviousSibling &&
        (ContainsBlankLine(source, previousEnd, childStart) || (previousEndColumn == 0 && childStartRow > previousEndRow))
    ) {
        AppendChild(parent, MakeBlankLine(model));
    }
    const bool isTrailingComment = childSyntax.kind == SyntaxNodeKind::Comment &&
        hasPreviousSibling &&
        previousEndRow == childStartRow;
    AppendTsNode(model, child, source, parent, childSyntax, isTrailingComment);
    previousEnd = childEnd;
    previousEndRow = childEndRow;
    previousEndColumn = childEndColumn;
    hasPreviousSibling = true;
}

void AppendTsChildren(
    FormatModel& model,
    TSNode tsNode,
    std::string_view source,
    SyntaxNode& parent,
    uint32_t childCount
) {
    if (childCount == 0) {
        return;
    }

    uint32_t previousEnd = ts_node_start_byte(tsNode);
    uint32_t previousEndRow = ts_node_start_point(tsNode).row;
    uint32_t previousEndColumn = ts_node_start_point(tsNode).column;
    bool hasPreviousSibling = !parent.children.empty();
    for (uint32_t index = 0; index < childCount; ++index) {
        TSNode child = ts_node_child(tsNode, index);
        const TSPoint childEndPoint = ts_node_end_point(child);
        AppendTsChild(
            model,
            child,
            ts_node_end_byte(child),
            childEndPoint.row,
            childEndPoint.column,
            source,
            parent,
            previousEnd,
            previousEndRow,
            previousEndColumn,
            hasPreviousSibling
        );
    }
}

struct ProblemNode {
    bool found = false;
    bool missing = false;
    TSNode node = {};
};

struct PreprocessorPlacementError {
    TSPoint point = {};
    std::string directiveLine;
};

ProblemNode FindFirstProblem(TSNode node) {
    if (ts_node_is_missing(node)) {
        return {.found = true, .missing = true, .node = node};
    }
    if (std::string_view(ts_node_type(node)) == "ERROR") {
        return {.found = true, .missing = false, .node = node};
    }

    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        TSNode child = ts_node_child(node, index);
        if (!ts_node_has_error(child) && !ts_node_is_missing(child)) {
            continue;
        }
        ProblemNode problem = FindFirstProblem(child);
        if (problem.found) {
            return problem;
        }
    }
    return {};
}

std::string_view NodeText(TSNode node, const std::string& source) {
    return NodeText(node, std::string_view(source));
}

std::string_view FirstLine(std::string_view text) {
    const size_t lineEnd = text.find_first_of("\r\n");
    return lineEnd == std::string_view::npos ? text : text.substr(0, lineEnd);
}

std::string TrimAsciiWhitespace(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (
        value.back() == ' ' ||
        value.back() == '\t' ||
        value.back() == '\r' ||
        value.back() == '\n'
    )) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

std::string PreprocessorDirectiveLine(TSNode node, const std::string& source) {
    return TrimAsciiWhitespace(FirstLine(NodeText(node, source)));
}

bool IsConditionalOpeningDirectiveLine(std::string_view line) {
    return StartsWith(line, "#if ") ||
        StartsWith(line, "#if\t") ||
        StartsWith(line, "#ifdef ") ||
        StartsWith(line, "#ifdef\t") ||
        StartsWith(line, "#ifndef ") ||
        StartsWith(line, "#ifndef\t");
}

bool IsCheckedPreprocessorDirective(std::string_view line) {
    return StartsWith(line, "#if ") ||
        StartsWith(line, "#if\t") ||
        StartsWith(line, "#ifdef ") ||
        StartsWith(line, "#ifdef\t") ||
        StartsWith(line, "#ifndef ") ||
        StartsWith(line, "#ifndef\t") ||
        StartsWith(line, "#include ") ||
        StartsWith(line, "#include\t");
}

bool IsAllowedPreprocessorContainer(SyntaxNodeKind kind) {
    return kind == SyntaxNodeKind::TranslationUnit ||
        kind == SyntaxNodeKind::DeclarationList ||
        kind == SyntaxNodeKind::CompoundStatement ||
        kind == SyntaxNodeKind::FieldDeclarationList ||
        kind == SyntaxNodeKind::EnumeratorList ||
        kind == SyntaxNodeKind::PreprocIf ||
        kind == SyntaxNodeKind::PreprocIfdef ||
        kind == SyntaxNodeKind::PreprocElse ||
        kind == SyntaxNodeKind::PreprocElif;
}

bool IsConditionalPreprocessorKind(SyntaxNodeKind kind) {
    return kind == SyntaxNodeKind::PreprocIf || kind == SyntaxNodeKind::PreprocIfdef;
}

bool IsAllowedAtomicConditionalNode(std::string_view treeType) {
    return treeType == "preproc_define_ifdef" ||
        treeType == "preproc_nested_define_ifdef" ||
        treeType == "preproc_define_elif_chain" ||
        treeType == "preproc_define_namespace_if" ||
        treeType == "conditional_extern_c_open" ||
        treeType == "conditional_extern_c_close";
}

bool IsForbiddenPreprocessorPlacement(TsNodeSyntax syntax, SyntaxNodeKind parentKind, std::string_view treeType) {
    if (syntax.kind == SyntaxNodeKind::PreprocInclude) {
        return parentKind != SyntaxNodeKind::Unknown && !IsAllowedPreprocessorContainer(parentKind);
    }
    if (!IsConditionalPreprocessorKind(syntax.kind)) {
        return false;
    }
    if (TsNodeSyntaxHasClass(syntax, TokenClass::AtomicPreprocessor)) {
        return !IsAllowedAtomicConditionalNode(treeType);
    }
    if (parentKind == SyntaxNodeKind::Unknown) {
        return false;
    }
    return !IsAllowedPreprocessorContainer(parentKind);
}

void CollectPreprocessorPlacementErrors(
    TSNode node,
    const std::string& source,
    SyntaxNodeKind parentKind,
    std::vector<PreprocessorPlacementError>& errors
) {
    const TsNodeSyntax syntax = GetTsNodeSyntax(node);
    const std::string_view treeType = ts_node_type(node);
    const std::string directiveLine = PreprocessorDirectiveLine(node, source);
    if (
        IsCheckedPreprocessorDirective(directiveLine) &&
        IsForbiddenPreprocessorPlacement(syntax, parentKind, treeType)
    ) {
        errors.push_back({ts_node_start_point(node), directiveLine});
    }

    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        CollectPreprocessorPlacementErrors(ts_node_child(node, index), source, syntax.kind, errors);
    }
}

bool IsIncludeDirectiveLine(std::string_view line) {
    return StartsWith(line, "#include ") || StartsWith(line, "#include\t");
}

bool IsEndifDirectiveLine(std::string_view line) {
    return line == "#endif" ||
        StartsWith(line, "#endif ") ||
        StartsWith(line, "#endif\t") ||
        StartsWith(line, "#endif//");
}

bool IsIgnorablePreprocessorTailLine(std::string_view line) {
    return line.empty() || StartsWith(line, "//") || StartsWith(line, "/*") || StartsWith(line, "*");
}

bool PreviousLineAllowsWholePreprocessorItem(std::string_view line) {
    std::string trimmed = TrimAsciiWhitespace(line);
    if (trimmed.empty()) {
        return true;
    }
    if (StartsWith(trimmed, "//") || StartsWith(trimmed, "/*") || StartsWith(trimmed, "*")) {
        return true;
    }
    if (trimmed[0] == '#') {
        return true;
    }
    const size_t lineComment = trimmed.find("//");
    if (lineComment != std::string::npos) {
        trimmed = TrimAsciiWhitespace(std::string_view(trimmed).substr(0, lineComment));
        if (trimmed.empty()) {
            return true;
        }
    }
    const char last = trimmed.back();
    return last == '{' || last == '}' || last == ';' || last == ':';
}

PreprocessorPlacementError MakeLinePlacementError(int row, std::string_view line) {
    const std::string trimmed = TrimAsciiWhitespace(line);
    return {
        TSPoint{static_cast<uint32_t>(row), static_cast<uint32_t>(line.size() - TrimLeadingWhitespace(line).size())},
        trimmed
    };
}

std::string_view LineText(const std::string& source, size_t start, size_t end) {
    if (end > start && source[end - 1] == '\r') {
        --end;
    }
    return std::string_view(source).substr(start, end - start);
}

void CollectIncludePlacementErrors(const std::string& source, std::vector<PreprocessorPlacementError>& errors) {
    size_t lineStart = 0;
    size_t previousNonEmptyStart = std::string::npos;
    size_t previousNonEmptyEnd = std::string::npos;
    int row = 0;
    while (lineStart <= source.size()) {
        size_t lineEnd = source.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = source.size();
        }
        const std::string_view line = LineText(source, lineStart, lineEnd);
        const std::string trimmed = TrimAsciiWhitespace(line);
        if (IsIncludeDirectiveLine(trimmed)) {
            const bool previousAllowed = previousNonEmptyStart == std::string::npos ||
                PreviousLineAllowsWholePreprocessorItem(LineText(source, previousNonEmptyStart, previousNonEmptyEnd));
            if (!previousAllowed) {
                errors.push_back(MakeLinePlacementError(row, line));
            }
        }
        if (!trimmed.empty()) {
            previousNonEmptyStart = lineStart;
            previousNonEmptyEnd = lineEnd;
        }
        if (lineEnd == source.size()) {
            break;
        }
        lineStart = lineEnd + 1;
        ++row;
    }
}

void CollectConditionalTailPlacementErrors(const std::string& source, std::vector<PreprocessorPlacementError>& errors) {
    std::vector<PreprocessorPlacementError> directiveStack;
    std::optional<PreprocessorPlacementError> pendingClosedDirective;
    size_t lineStart = 0;
    int row = 0;
    while (lineStart <= source.size()) {
        size_t lineEnd = source.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = source.size();
        }
        const std::string_view line = LineText(source, lineStart, lineEnd);
        const std::string trimmed = TrimAsciiWhitespace(line);
        if (IsConditionalOpeningDirectiveLine(trimmed)) {
            directiveStack.push_back(MakeLinePlacementError(row, line));
            pendingClosedDirective.reset();
        } else if (IsEndifDirectiveLine(trimmed)) {
            if (!directiveStack.empty()) {
                pendingClosedDirective = directiveStack.back();
                directiveStack.pop_back();
            } else {
                pendingClosedDirective.reset();
            }
        } else if (!IsIgnorablePreprocessorTailLine(trimmed)) {
            if (pendingClosedDirective.has_value() && StartsWith(trimmed, "<<")) {
                errors.push_back(*pendingClosedDirective);
            }
            pendingClosedDirective.reset();
        }
        if (lineEnd == source.size()) {
            break;
        }
        lineStart = lineEnd + 1;
        ++row;
    }
}

std::string FormatPreprocessorPlacementErrors(const std::vector<PreprocessorPlacementError>& errors) {
    std::string text;
    for (const PreprocessorPlacementError& error : errors) {
        if (!text.empty()) {
            text += '\n';
        }
        text += "unsupported preprocessor placement at ";
        text += std::to_string(static_cast<int>(error.point.row) + 1);
        text += ":";
        text += std::to_string(static_cast<int>(error.point.column) + 1);
        text += ": ";
        text += error.directiveLine;
    }
    return text;
}

void SortAndDeduplicatePreprocessorPlacementErrors(std::vector<PreprocessorPlacementError>& errors) {
    std::sort(errors.begin(), errors.end(), [](const auto& left, const auto& right) {
        if (left.point.row != right.point.row) {
            return left.point.row < right.point.row;
        }
        if (left.point.column != right.point.column) {
            return left.point.column < right.point.column;
        }
        return left.directiveLine < right.directiveLine;
    });
    errors.erase(std::unique(errors.begin(), errors.end(), [](const auto& left, const auto& right) {
        return left.point.row == right.point.row &&
            left.point.column == right.point.column &&
            left.directiveLine == right.directiveLine;
    }), errors.end());
}

ParseResult PreprocessorPlacementFailure(TSNode root, const std::string& source) {
    std::vector<PreprocessorPlacementError> errors;
    CollectIncludePlacementErrors(source, errors);
    CollectConditionalTailPlacementErrors(source, errors);
    CollectPreprocessorPlacementErrors(root, source, SyntaxNodeKind::Unknown, errors);
    SortAndDeduplicatePreprocessorPlacementErrors(errors);
    ParseResult parse;
    if (errors.empty()) {
        parse.ok = true;
        return parse;
    }
    parse.ok = false;
    parse.error = FormatPreprocessorPlacementErrors(errors);
    return parse;
}

void AppendIncludeRun(
    SyntaxChildList& sourceChildren,
    size_t& index,
    SyntaxChildList& groupedChildren,
    FormatModel& model,
    SyntaxNode& root
) {
    SyntaxNode* includeRun = MakeNode(model);
    includeRun->kind = SyntaxNodeKind::IncludeRun;
    includeRun->parent = &root;
    includeRun->depth = root.depth + 1;

    for (; index < sourceChildren.size(); ++index) {
        if (
            sourceChildren[index] != nullptr &&
            SyntaxNodeKindHasClass(sourceChildren[index]->kind, TokenClass::IncludeDirective)
        ) {
            AppendChild(*includeRun, sourceChildren[index]);
            continue;
        }
        if (sourceChildren[index] != nullptr && sourceChildren[index]->kind == SyntaxNodeKind::BlankLine) {
            size_t nextIndex = index + 1;
            while (
                nextIndex < sourceChildren.size() &&
                sourceChildren[nextIndex] != nullptr &&
                sourceChildren[nextIndex]->kind == SyntaxNodeKind::BlankLine
            ) {
                ++nextIndex;
            }
            if (
                nextIndex < sourceChildren.size() &&
                sourceChildren[nextIndex] != nullptr &&
                SyntaxNodeKindHasClass(sourceChildren[nextIndex]->kind, TokenClass::IncludeDirective)
            ) {
                AppendChild(*includeRun, sourceChildren[index]);
                index = nextIndex - 1;
                continue;
            }
            index = nextIndex;
            break;
        }
        break;
    }

    groupedChildren.push_back(includeRun);
}

bool IsPragmaOnceNode(const SyntaxNode& node) {
    return node.kind == SyntaxNodeKind::PreprocCall && StartsWith(TrimLeadingWhitespace(node.text), "#pragma once");
}

bool IsPreprocessorConditionHeaderNode(const SyntaxNode& node) {
    return node.kind == SyntaxNodeKind::FreeToken || node.kind == SyntaxNodeKind::Identifier;
}

bool IsStructuredIncludeGuardOwner(SyntaxNodeKind kind) {
    return kind == SyntaxNodeKind::PreprocIfdef || kind == SyntaxNodeKind::PreprocIf;
}

bool CanRemainInOpeningIncludeArea(const SyntaxNode& owner, const SyntaxNode& child, bool sawInclude) {
    if (SyntaxNodeKindHasClass(child.kind, TokenClass::Trivia)) {
        return true;
    }
    if (sawInclude) {
        return false;
    }
    if (owner.kind == SyntaxNodeKind::TranslationUnit) {
        return IsPragmaOnceNode(child);
    }
    if (IsStructuredIncludeGuardOwner(owner.kind)) {
        return child.kind == SyntaxNodeKind::PreprocDef || IsPreprocessorConditionHeaderNode(child);
    }
    return false;
}

void GroupOpeningIncludeRuns(FormatModel& model, SyntaxNode& root) {
    for (SyntaxNode* child : root.children) {
        if (child != nullptr) {
            GroupOpeningIncludeRuns(model, *child);
        }
    }

    if (root.kind != SyntaxNodeKind::TranslationUnit && !IsStructuredIncludeGuardOwner(root.kind)) {
        return;
    }

    SyntaxChildList groupedChildren(root.children.get_allocator());
    groupedChildren.reserve(root.children.size());
    bool sawInclude = false;
    bool inOpeningArea = true;
    for (size_t index = 0; index < root.children.size();) {
        if (inOpeningArea && root.children[index] != nullptr && SyntaxNodeKindHasClass(
            root.children[index]->kind,
            TokenClass::IncludeDirective
        )) {
            AppendIncludeRun(root.children, index, groupedChildren, model, root);
            sawInclude = true;
            continue;
        }
        const bool canRemainInOpeningArea = inOpeningArea &&
            root.children[index] != nullptr &&
            CanRemainInOpeningIncludeArea(root, *root.children[index], sawInclude);
        if (canRemainInOpeningArea) {
            groupedChildren.push_back(root.children[index]);
            ++index;
            continue;
        }

        inOpeningArea = false;
        groupedChildren.push_back(root.children[index]);
        ++index;
    }

    root.children = std::move(groupedChildren);
}

ParseResult ParseFailure(TSNode root) {
    ProblemNode problem = FindFirstProblem(root);
    if (!problem.found) {
        problem = {.found = true, .missing = false, .node = root};
    }
    const TSPoint point = ts_node_start_point(problem.node);
    const std::string nodeType = problem.missing ? "missing " + std::string(ts_node_type(problem.node)) :
        std::string(ts_node_type(problem.node));
    ParseResult parse;
    parse.ok = false;
    parse.error = "tree-sitter parse failed at " +
        std::to_string(static_cast<int>(point.row) + 1) +
        ":" +
        std::to_string(static_cast<int>(point.column) + 1) +
        " near " +
        nodeType;
    return parse;
}

}  // namespace

FormatModel BuildFormatModel(TSNode root, std::unique_ptr<std::string> sourceText) {
    FormatModel model;
    model.sourceText = std::move(sourceText);
    if (!model.sourceText) {
        model.parse.error = "formatter source ownership setup failed";
        return model;
    }

    const std::string_view source(*model.sourceText);
    model.nodes.reserve(source.size() * 2 + 64);
    model.parse = PreprocessorPlacementFailure(root, *model.sourceText);
    if (!model.parse.ok) {
        return model;
    }
    if (ts_node_has_error(root) || ts_node_is_missing(root)) {
        model.parse = ParseFailure(root);
        return model;
    }

    model.root = BuildNode(model, root, source, nullptr, GetTsNodeSyntax(root));
    GroupOpeningIncludeRuns(model, *model.root);
    model.parse.ok = true;
    return model;
}
