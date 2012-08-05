# Synapse Chromium

A Chrome extension that runs Synapse on a network peer from Google Chrome.

## Publishing an Extension

Remove references to development servers (i.e. localhost) from `manifest.json`. 

If you're on OS X, create the following extension:

```console
$ alias chrome='/Applications/Google\ Chrome.app/Contents/MacOS/Google\ Chrome'
```

Now you can sign the package with the following.

```console
$ chrome --pack-extension=chromium --pack-extension-key=chromium.pem --no-message-box
```

You will see a message box, despite your request. I keep adding that switch in
hopes that it will someday make the message box go away, as the documentation
says it should.

Rename the output file to reflect the platform and version, where platform is
osx, linux, windows, and version is the release number.
