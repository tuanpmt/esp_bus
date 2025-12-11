# pytest-embedded configuration
import pytest

@pytest.fixture(scope='session')
def app_path():
    return '.'

