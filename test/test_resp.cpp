#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "protocol/respEncoder.hpp"
#include "protocol/respObject.hpp"
#include "protocol/respParser.hpp"

TEST(RESPEncoderTest, SimpleValues) {
    EXPECT_EQ(RESPEncoder::simpleString("OK"), "+OK\r\n");
    EXPECT_EQ(RESPEncoder::error("ERR bad"), "-ERR bad\r\n");
    EXPECT_EQ(RESPEncoder::integer(123), ":123\r\n");
    EXPECT_EQ(RESPEncoder::bulkString("GET"), "$3\r\nGET\r\n");
    EXPECT_EQ(RESPEncoder::nullBulk(), "$-1\r\n");
}

TEST(RESPEncoderTest, ArrayAsBulkStrings) {
    const std::string got = RESPEncoder::array({"SET", "k", "v"});
    const std::string expect = "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n";
    EXPECT_EQ(got, expect);
}

TEST(RESPParserTest, ParseSimpleString) {
    RESPParser parser;
    parser.feed("+PONG\r\n", 7);

    RESPObject out;
    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RESPType::SIMPLE_STRING);
    EXPECT_EQ(out.str, "PONG");
}

TEST(RESPParserTest, ParseBulkStringWithPartialFeed) {
    RESPParser parser;
    parser.feed("$5\r\nhe", 6);

    RESPObject out;
    EXPECT_FALSE(parser.parse(out));

    parser.feed("llo\r\n", 5);
    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RESPType::BULK_STRING);
    EXPECT_EQ(out.str, "hello");
}

TEST(RESPParserTest, ParseArray) {
    RESPParser parser;
    const std::string in = "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    ASSERT_TRUE(parser.parse(out));
    ASSERT_EQ(out.type, RESPType::ARRAY);
    ASSERT_EQ(out.elements.size(), 2u);
    EXPECT_EQ(out.elements[0].type, RESPType::BULK_STRING);
    EXPECT_EQ(out.elements[0].str, "GET");
    EXPECT_EQ(out.elements[1].type, RESPType::BULK_STRING);
    EXPECT_EQ(out.elements[1].str, "key");
}

TEST(RESPParserTest, ParseTwoMessagesInSequence) {
    RESPParser parser;
    const std::string in = "+OK\r\n:7\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out1;
    ASSERT_TRUE(parser.parse(out1));
    EXPECT_EQ(out1.type, RESPType::SIMPLE_STRING);
    EXPECT_EQ(out1.str, "OK");

    RESPObject out2;
    ASSERT_TRUE(parser.parse(out2));
    EXPECT_EQ(out2.type, RESPType::INTEGER);
    EXPECT_EQ(out2.integer, 7);
}

TEST(RESPParserTest, ParseNullBulkString) {
    RESPParser parser;
    const std::string in = "$-1\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RESPType::NULL_BULK);
}

TEST(RESPParserTest, ParseEmptyBulkString) {
    RESPParser parser;
    const std::string in = "$0\r\n\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RESPType::BULK_STRING);
    EXPECT_EQ(out.str, "");
}

TEST(RESPParserTest, ParseEmptyArray) {
    RESPParser parser;
    const std::string in = "*0\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RESPType::ARRAY);
    EXPECT_TRUE(out.elements.empty());
}

TEST(RESPParserTest, ParseArrayWithPartialSecondElement) {
    RESPParser parser;
    const std::string partial = "*2\r\n$3\r\nGET\r\n$3\r\nke";
    parser.feed(partial.data(), partial.size());

    RESPObject out;
    EXPECT_FALSE(parser.parse(out));

    parser.feed("y\r\n", 3);
    ASSERT_TRUE(parser.parse(out));
    ASSERT_EQ(out.type, RESPType::ARRAY);
    ASSERT_EQ(out.elements.size(), 2u);
    EXPECT_EQ(out.elements[0].str, "GET");
    EXPECT_EQ(out.elements[1].str, "key");
}

TEST(RESPParserTest, ParseBulkStringWithSplitTerminator) {
    RESPParser parser;
    const std::string partial = "$3\r\nabc\r";
    parser.feed(partial.data(), partial.size());

    RESPObject out;
    EXPECT_FALSE(parser.parse(out));

    parser.feed("\n", 1);
    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RESPType::BULK_STRING);
    EXPECT_EQ(out.str, "abc");
}

TEST(RESPParserTest, ParseEmptyFeedReturnsFalse) {
    RESPParser parser;
    parser.feed(nullptr, 0);
    parser.feed("", 0);

    RESPObject out;
    EXPECT_FALSE(parser.parse(out));
}

TEST(RESPParserTest, IncompleteSimpleStringReturnsFalse) {
    RESPParser parser;
    const std::string in = "+PONG\r";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_FALSE(parser.parse(out));
}

TEST(RESPParserTest, InvalidTypePrefixThrows) {
    RESPParser parser;
    const std::string in = "?\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

TEST(RESPParserTest, InvalidIntegerContentThrows) {
    RESPParser parser;
    const std::string in = ":abc\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_THROW(parser.parse(out), std::exception);
}

TEST(RESPParserTest, InvalidIntegerWithTrailingCharactersThrows) {
    RESPParser parser;
    const std::string in = ":123abc\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

TEST(RESPParserTest, InvalidBulkLengthThrows) {
    RESPParser parser;
    const std::string in = "$x\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_THROW(parser.parse(out), std::exception);
}

TEST(RESPParserTest, InvalidBulkLengthWithTrailingCharactersThrows) {
    RESPParser parser;
    const std::string in = "$3abc\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

TEST(RESPParserTest, BulkLengthLessThanMinusOneThrows) {
    RESPParser parser;
    const std::string in = "$-2\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

TEST(RESPParserTest, NullArrayThrows) {
    RESPParser parser;
    const std::string in = "*-1\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

TEST(RESPParserTest, OverflowIntegerThrows) {
    RESPParser parser;
    const std::string in = ":9223372036854775808\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_THROW(parser.parse(out), std::exception);
}

TEST(RESPParserTest, InvalidBulkTerminatorThrows) {
    RESPParser parser;
    const std::string in = "$3\r\nabcxx";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

TEST(RESPParserTest, InvalidArrayLengthThrows) {
    RESPParser parser;
    const std::string in = "*x\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_THROW(parser.parse(out), std::exception);
}

TEST(RESPParserTest, InvalidArrayLengthWithTrailingCharactersThrows) {
    RESPParser parser;
    const std::string in = "*2x\r\n";
    parser.feed(in.data(), in.size());

    RESPObject out;
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}
