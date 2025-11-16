import XCTest
import SwiftTreeSitter
import TreeSitterToon

final class TreeSitterToonTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_toon())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Toon grammar")
    }
}
