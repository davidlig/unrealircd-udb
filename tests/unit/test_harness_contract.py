"""The C harness protocol must not hide missing or failing cases."""

import pytest

from udb_test_support.cases import Case, parse_case_list


@pytest.mark.tooling
def test_case_listing_has_stable_suite_and_case_names():
    assert parse_case_list('[{"suite":"numeric","name":"overflow"}]') == [
        Case("numeric", "overflow")
    ]


@pytest.mark.tooling
@pytest.mark.parametrize("listing", ["[]", "{}", "not json", '[{"suite":"x"}]',
    '[{"suite":"x","name":"y"},{"suite":"x","name":"y"}]',
    '[{"suite":"../x","name":"y"}]', '[{"suite":"x","name":"y*"}]'])
def test_empty_malformed_duplicate_or_unsafe_listings_fail(listing):
    with pytest.raises(ValueError):
        parse_case_list(listing)
