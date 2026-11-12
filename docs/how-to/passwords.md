**English** · [Italiano](../it/how-to/password.md)

# Type a password without writing it in scripts

Scripts are in plain text on the microSD and are easy to copy. Passwords therefore go in `config.lua` and are typed with `kb.type_secret`, which never returns them to the script as a value.

In `config.lua`:

```lua
secrets = { admin = "the-password" },
```

In the script:

```lua
kb.type("Administrator\t")
kb.type_secret("admin")
kb.tap("ENTER")
```

The script cannot read the value: `cfg("secrets.admin")` always returns `nil`. A nonexistent name gives the error `secret 'name' not in config.lua`.

## What it does not protect

Passwords are still in plain text in `config.lua`, and anyone with the card can read them. Moreover, the script decides where the password is typed: if it types it into a terminal instead of a password field, it can then read it back from the PC with `host.get`. `type_secret` prevents reading it by mistake, not a script written to do so. They are acceptable only for installation credentials you accept exposing this way. See [Security](../explanation/security.md).
