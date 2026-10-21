class RicohSp200PrinterApp < Formula
  desc "PAPPL printer application for RICOH SP 200-series (SP 202SN)"
  homepage "https://github.com/klinkov/ricoh-sp200-printer-app"
  url "https://github.com/klinkov/ricoh-sp200-printer-app/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "81524f658fd1e6b143803cf91ba58b332e5cba1da6c43d111b0496752e824fe9"
  head "https://github.com/klinkov/ricoh-sp200-printer-app.git", branch: "main"
  license "MIT"

  depends_on "pkg-config" => :build
  depends_on "jbigkit"
  depends_on "shawnz/printing/pappl"

  def install
    system "make", "app"
    system "make", "install", "PREFIX=#{prefix}"
  end

  service do
    run [opt_bin/"ricoh-sp200-printer-app", "server", "-o", "server-port=8000",
         "-o", "log-file=#{var}/log/ricoh-sp200-printer-app.log"]
    keep_alive true
    working_dir var
  end

  test do
    assert_match "0.1.0", shell_output("#{bin}/ricoh-sp200-printer-app --version")
  end
end
