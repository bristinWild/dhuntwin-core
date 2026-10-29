{
  description = "DhunTwin Core — the DhunTwin OS digital-twin engine as a Logos core module";

  inputs = {
    # 0.3.x is the module generation current Basecamp loads.
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.3.3";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
