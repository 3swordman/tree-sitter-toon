package tree_sitter_toon_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_toon "github.com/3swordman/tree-sitter-toon/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_toon.Language())
	if language == nil {
		t.Errorf("Error loading Toon grammar")
	}
}
