class Nfcidm < Formula
  desc "Read a FeliCa IDm with native macOS USB and PC/SC APIs"
  homepage "https://github.com/takesako/nfcidm"
  url "https://github.com/takesako/nfcidm/releases/download/v0.5.3/nfcidm-0.5.3.tar.gz"
  sha256 "b36b4c8c99b42c36b6294c52799e0d6e5157f620853424a23115e4c98c8ba0a1"
  license "MIT"

  depends_on arch: :arm64
  depends_on macos: :sequoia

  def install
    system "make", "CC=#{ENV.cc}", "test"
    system "make", "CC=#{ENV.cc}"
    bin.install "nfcidm"
  end

  test do
    assert_equal "nfcidm #{version}\n", shell_output("#{bin}/nfcidm --version")
    assert_match "Usage: nfcidm", shell_output("#{bin}/nfcidm --help")
  end
end
