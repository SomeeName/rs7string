"""Optional private-only extras of the checkout: maintainer test tools that stay out of the public tree (.publicignore)
and out of a public build (private.props, build.ps1 -Public). Their test data lives in tools/private_extras.py; in a
public tree that module is absent and every hook here is a no-op."""

from __future__ import annotations

try:
    import private_extras
except ModuleNotFoundError as e:  # absent in the public tree; a broken private_extras must not hide its tests
    if e.name != "private_extras":
        raise
    private_extras = None


def get(name: str, default):
    """A value the private extras add (site ids, exports, fixtures), else `default`."""
    return getattr(private_extras, name, default) if private_extras else default


def extend(module: str, namespace: dict) -> None:
    """Lets the private extras add their cases to the test module `module` (its globals)."""
    hook = getattr(private_extras, module.rsplit(".", 1)[-1], None) if private_extras else None
    if hook:
        hook(namespace)
