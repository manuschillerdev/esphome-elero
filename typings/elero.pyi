# The pytest fixture loads components/elero as a top-level module via sys.path.
# Re-export that package for static analysis without changing the runtime tests.
from components.elero import *  # noqa: F403
from components.elero import (
    _validate_irq_pin as _validate_irq_pin,
)
from components.elero import (
    _validate_sx1262_pins as _validate_sx1262_pins,
)
