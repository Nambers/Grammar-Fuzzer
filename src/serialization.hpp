#ifndef SERIALIZATION_HPP
#define SERIALIZATION_HPP

#include "ast.hpp"
#include <nlohmann/json.hpp>

namespace FuzzingAST {
inline void to_json(nlohmann::json &j, const ASTNodeValue &node) {
	// {"t": TYPE_INDEX, "v": VALUE}
	j["t"] = node.val.index();
	switch (node.val.index()) {
	case 0:
		j["v"] = std::get<std::string>(node.val);
		break;
	case 1:
		j["v"] = std::get<int64_t>(node.val);
		break;
	case 2:
		j["v"] = std::get<bool>(node.val);
		break;
	case 3:
		j["v"] = std::get<double>(node.val);
		break;
	case 4:
		j["v"] = std::get<NodeID>(node.val);
		break;
	};
}

inline void from_json(const nlohmann::json &j, ASTNodeValue &node) {
	// {"t": TYPE_INDEX, "v": VALUE}
	auto index = j.at("t").template get<size_t>();
	switch (index) {
	case 0:
		node.val = j.at("v").template get<std::string>();
		break;
	case 1:
		node.val = j.at("v").template get<int64_t>();
		break;
	case 2:
		node.val = j.at("v").template get<bool>();
		break;
	case 3:
		node.val = j.at("v").template get<double>();
		break;
	case 4:
		node.val = j.at("v").template get<NodeID>();
		break;
	default:
		throw std::runtime_error("Invalid ASTNodeValue type");
	}
}

}; // namespace FuzzingAST

namespace nlohmann {
template <> struct adl_serializer<::FuzzingAST::ASTNodeValue> {
	static void to_json(json &j, const ::FuzzingAST::ASTNodeValue &v) {
		::FuzzingAST::to_json(j, v);
	}
	static void from_json(const json &j, ::FuzzingAST::ASTNodeValue &v) {
		::FuzzingAST::from_json(j, v);
	}
};
}; // namespace nlohmann

namespace FuzzingAST {
NLOHMANN_JSON_SERIALIZE_ENUM(ASTNodeKind,
							 {{ASTNodeKind::Function, "Function"},
							  {ASTNodeKind::Class, "Class"},
							  {ASTNodeKind::DeclareVar, "DeclareVar"},
							  {ASTNodeKind::Import, "Import"},
							  {ASTNodeKind::Assign, "Assign"},
							  {ASTNodeKind::Call, "Call"},
							  {ASTNodeKind::Return, "Return"},
							  {ASTNodeKind::BinaryOp, "BinaryOp"},
							  {ASTNodeKind::UnaryOp, "UnaryOp"},
							  {ASTNodeKind::Literal, "Literal"}});
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ASTNode, kind, fields, scope, type);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ASTScope, declarations, expressions);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FunctionSignature, paramTypes, selfType,
								   returnType);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AST, scopes, declarations, expressions);

}; // namespace FuzzingAST

#endif // SERIALIZATION_HPP
